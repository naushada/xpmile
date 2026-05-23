#include "idp_token.hpp"

#include <cstdint>
#include <string>

#include "json.hpp"
#include "jwt_signer.hpp"
#include "mongodbc.hpp"
#include "sso_oidc.hpp"   // sso::code_challenge (PKCE S256)
#include "sso_util.hpp"   // sso::random_token, sso::base64url_encode

namespace idp {

namespace {

inline constexpr long kAccessTokenTtlSec = 3600;  // 1 hour
inline constexpr long kIdTokenTtlSec     = 3600;  // 1 hour

const std::string &get(const std::map<std::string, std::string> &m,
                        const std::string &k) {
  static const std::string empty;
  auto it = m.find(k);
  return (it == m.end()) ? empty : it->second;
}

sso::SsoHttpResult error_response(int status, const std::string &error,
                                    const std::string &description = {}) {
  sso::SsoHttpResult r;
  r.status       = status;
  r.content_type = "application/json";
  nlohmann::json body = {{"error", error}};
  if (!description.empty()) body["error_description"] = description;
  r.body = body.dump();
  return r;
}

/// base64url-encode a UTF-8 string (no padding).
std::string b64u_str(const std::string &s) {
  return sso::base64url_encode(
      reinterpret_cast<const unsigned char *>(s.data()), s.size());
}

/// Build the to-be-signed JWT bytes: b64u(header) + "." + b64u(payload).
std::string make_to_sign(const nlohmann::json &header,
                          const nlohmann::json &payload) {
  return b64u_str(header.dump()) + "." + b64u_str(payload.dump());
}

} // namespace

sso::SsoHttpResult token(
    const std::map<std::string, std::string> &form,
    IMongodbClient &db,
    IJwtSigner &signer,
    const std::string &issuer,
    sso::IClock &clock) {

  // ── parse form ────────────────────────────────────────────────────────
  const std::string &grant_type    = get(form, "grant_type");
  const std::string &code          = get(form, "code");
  const std::string &code_verifier = get(form, "code_verifier");
  const std::string &client_id     = get(form, "client_id");
  const std::string &redirect_uri  = get(form, "redirect_uri");

  if (grant_type != "authorization_code") {
    return error_response(400, "unsupported_grant_type",
                           "only authorization_code is supported");
  }
  if (code.empty())          return error_response(400, "invalid_request", "missing code");
  if (code_verifier.empty()) return error_response(400, "invalid_request",
                                                     "missing code_verifier (PKCE)");
  if (client_id.empty())     return error_response(400, "invalid_request", "missing client_id");
  if (redirect_uri.empty())  return error_response(400, "invalid_request", "missing redirect_uri");

  // ── look up the code ──────────────────────────────────────────────────
  const nlohmann::json code_query = {{"_id", code}};
  const std::string code_raw =
      db.get_document("idp_codes", code_query.dump(), "{}");
  if (code_raw.empty()) {
    return error_response(400, "invalid_grant", "code not found");
  }
  nlohmann::json code_doc;
  try {
    code_doc = nlohmann::json::parse(code_raw);
  } catch (const std::exception &) {
    return error_response(500, "server_error", "malformed code doc");
  }

  const std::int64_t now = clock.now_unix();
  if (code_doc.value("expiresAt", std::int64_t{0}) <= now) {
    return error_response(400, "invalid_grant", "code expired");
  }
  if (code_doc.value("client_id", std::string{}) != client_id) {
    return error_response(400, "invalid_grant", "client_id mismatch");
  }
  if (code_doc.value("redirect_uri", std::string{}) != redirect_uri) {
    return error_response(400, "invalid_grant", "redirect_uri mismatch");
  }

  // PKCE: S256(verifier) must equal stored challenge.
  const std::string stored_challenge =
      code_doc.value("code_challenge", std::string{});
  const std::string computed_challenge = sso::code_challenge(code_verifier);
  if (stored_challenge.empty() || stored_challenge != computed_challenge) {
    return error_response(400, "invalid_grant",
                           "PKCE verifier does not match challenge");
  }

  // ── atomic claim ──────────────────────────────────────────────────────
  // Filter requires `consumed` to NOT exist; $set marks it true. If the
  // update modifies 0 docs, someone else won the race → 400 invalid_grant.
  const nlohmann::json claim_filter = {
      {"_id", code},
      {"consumed", {{"$exists", false}}},
  };
  const nlohmann::json claim_update = {
      {"$set", {{"consumed", true}}},
  };
  if (!db.update_collection("idp_codes",
                              claim_filter.dump(),
                              claim_update.dump())) {
    return error_response(400, "invalid_grant", "code already used");
  }

  // ── build the id_token (RS256 signed via the supplied IJwtSigner) ────
  //
  // The header includes `kid`, which must be the kid actually used for
  // signing. The signer is the source of truth (the on-prem dispatcher
  // resolves "current" → the active key's real kid). We sign once with
  // a placeholder kid, get the resolved kid back, then re-sign with the
  // correct kid baked into the header. Costs one extra wsdbagent round
  // trip per token; v1 acceptable. (See design §11 open question on
  // caching the active kid on the cloud side.)
  nlohmann::json header = {
      {"alg", "RS256"},
      {"typ", "JWT"},
      {"kid", "current"},
  };
  nlohmann::json payload = {
      {"iss", issuer},
      {"sub", code_doc.value("user_sub", std::string{})},
      {"aud", client_id},
      {"exp", now + kIdTokenTtlSec},
      {"iat", now},
  };
  if (auto v = code_doc.value("nonce",      std::string{}); !v.empty()) payload["nonce"] = v;
  if (auto v = code_doc.value("user_email", std::string{}); !v.empty()) payload["email"] = v;
  if (auto v = code_doc.value("user_name",  std::string{}); !v.empty()) payload["name"]  = v;
  if (auto v = code_doc.value("user_role",  std::string{}); !v.empty()) payload["role"]  = v;

  // First sign — resolves "current" → real kid.
  std::string to_sign = make_to_sign(header, payload);
  SignJwtResult sig = signer.sign("current", "RS256", to_sign);
  if (!sig.ok) {
    return error_response(500, "server_error",
                           "signing failed: " + sig.error);
  }

  // If the resolved kid differs from our placeholder, rebuild header
  // with the real kid and re-sign so the signed bytes match the header.
  if (!sig.kid.empty() && sig.kid != "current") {
    header["kid"] = sig.kid;
    to_sign      = make_to_sign(header, payload);
    sig          = signer.sign(sig.kid, "RS256", to_sign);
    if (!sig.ok) {
      return error_response(500, "server_error",
                             "re-sign with resolved kid failed: " + sig.error);
    }
  }

  const std::string id_token = to_sign + "." + sig.signature;

  // ── access_token (opaque, stored for /userinfo lookup) ────────────────
  const std::string access_token = sso::random_token(32);
  nlohmann::json at_doc = {
      {"_id",         access_token},
      {"client_id",   client_id},
      {"sub",         code_doc.value("user_sub",   std::string{})},
      {"email",       code_doc.value("user_email", std::string{})},
      {"name",        code_doc.value("user_name",  std::string{})},
      {"role",        code_doc.value("user_role",  std::string{})},
      {"scope",       code_doc.value("scope",      std::string{})},
      {"expiresAt",   now + kAccessTokenTtlSec},
  };
  db.create_document("idp", "idp_access_tokens", at_doc.dump());

  // ── response ──────────────────────────────────────────────────────────
  nlohmann::json body = {
      {"access_token", access_token},
      {"token_type",   "Bearer"},
      {"expires_in",   kAccessTokenTtlSec},
      {"id_token",     id_token},
      {"scope",        code_doc.value("scope", std::string{})},
  };
  sso::SsoHttpResult r;
  r.status       = 200;
  r.content_type = "application/json";
  r.body         = body.dump();
  return r;
}

} // namespace idp
