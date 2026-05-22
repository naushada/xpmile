#include "sso_oidc.hpp"

#include <map>
#include <utility>

#include <openssl/sha.h>

#include "json.hpp"
#include "mongodbc.hpp"
#include "sso_jwt.hpp"
#include "sso_session.hpp"
#include "sso_util.hpp"

namespace sso {

std::string make_code_verifier() {
  // 32 bytes of entropy → 43 base64url chars, all within the PKCE
  // unreserved set [A-Za-z0-9-_].
  return random_token(32);
}

std::string code_challenge(const std::string &verifier) {
  unsigned char digest[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const unsigned char *>(verifier.data()),
         verifier.size(), digest);
  return base64url_encode(digest, SHA256_DIGEST_LENGTH);
}

bool fetch_discovery(IHttpClient &http, const std::string &issuer,
                     OidcEndpoints &out, std::string &error) {
  std::string base = issuer;
  if (!base.empty() && base.back() == '/') base.pop_back();
  const std::string url = base + "/.well-known/openid-configuration";

  const HttpResponse resp = http.get(url);
  if (!resp.ok()) {
    error = "OIDC discovery request failed";
    return false;
  }

  nlohmann::json j =
      nlohmann::json::parse(resp.body, nullptr, /*allow_exceptions=*/false);
  if (j.is_discarded() || !j.is_object()) {
    error = "malformed OIDC discovery document";
    return false;
  }

  OidcEndpoints ep;
  ep.issuer                 = j.value("issuer", std::string{});
  ep.authorization_endpoint = j.value("authorization_endpoint", std::string{});
  ep.token_endpoint         = j.value("token_endpoint", std::string{});
  ep.jwks_uri               = j.value("jwks_uri", std::string{});
  ep.end_session_endpoint   = j.value("end_session_endpoint", std::string{});

  // The document's `issuer` MUST match the configured issuer
  // (OpenID Connect Discovery §4.3) — guards against a swapped document.
  if (ep.issuer != issuer) {
    error = "OIDC discovery issuer mismatch";
    return false;
  }
  if (ep.authorization_endpoint.empty() || ep.token_endpoint.empty() ||
      ep.jwks_uri.empty()) {
    error = "OIDC discovery document is missing a required endpoint";
    return false;
  }

  out = ep;
  return true;
}

OidcProvider::OidcProvider(ProviderConfig config, OidcEndpoints endpoints,
                           std::string public_base_url, IMongodbClient &db,
                           IHttpClient &http, IClock &clock)
    : m_config(std::move(config)),
      m_endpoints(std::move(endpoints)),
      m_public_base_url(std::move(public_base_url)),
      m_db(db),
      m_http(http),
      m_clock(clock) {}

AuthnRequest OidcProvider::begin_login(const std::string &return_to) {
  const std::string state     = random_token(32);
  const std::string nonce     = random_token(32);
  const std::string verifier  = make_code_verifier();
  const std::string challenge = code_challenge(verifier);

  // Persist the one-time login transaction, keyed by state. code_verifier and
  // nonce stay server-side until the callback consumes this document.
  nlohmann::json txn;
  txn["_id"]          = state;
  txn["provider"]     = m_config.id;
  txn["nonce"]        = nonce;
  txn["codeVerifier"] = verifier;
  txn["returnTo"]     = return_to;
  txn["createdAt"]    = m_clock.now_unix();
  m_db.create_document(m_db.get_database(), "sso_transactions", txn.dump());

  // redirect_uri is pinned to the configured public base URL — never derived
  // from a request header (sso-design.md §10).
  const std::string redirect_uri =
      m_public_base_url + "/api/v1/sso/callback/" + m_config.id;

  std::string scope;
  for (const auto &s : m_config.scopes) {
    if (!scope.empty()) scope += ' ';
    scope += s;
  }

  const std::map<std::string, std::string> params = {
      {"response_type", "code"},
      {"client_id", m_config.client_id},
      {"redirect_uri", redirect_uri},
      {"scope", scope},
      {"state", state},
      {"nonce", nonce},
      {"code_challenge", challenge},
      {"code_challenge_method", "S256"}};

  AuthnRequest req;
  req.redirect_url =
      m_endpoints.authorization_endpoint + "?" + encode_form(params);
  req.transaction_id = state;
  return req;
}

IdentityClaims OidcProvider::handle_callback(const std::string &code,
                                             const std::string &state) {
  IdentityClaims out;
  if (code.empty() || state.empty()) {
    out.error = "missing code or state";
    return out;
  }

  // 1. Read the login transaction.
  nlohmann::json id_query;
  id_query["_id"] = state;
  const std::string raw =
      m_db.get_document("sso_transactions", id_query.dump(), "{}");
  if (raw.empty()) {
    out.error = "unknown or expired state";
    return out;
  }
  nlohmann::json txn =
      nlohmann::json::parse(raw, nullptr, /*allow_exceptions=*/false);
  if (txn.is_discarded() || !txn.is_object()) {
    out.error = "corrupt transaction";
    return out;
  }
  const std::string nonce    = txn.value("nonce", std::string{});
  const std::string verifier = txn.value("codeVerifier", std::string{});
  out.return_to              = txn.value("returnTo", std::string{});

  // 2. Atomically claim the transaction — single-use. The guarded update
  //    matches only while `consumed` is absent, so a replay fails here.
  nlohmann::json claim_filter;
  claim_filter["_id"]                 = state;
  claim_filter["consumed"]["$exists"] = false;
  nlohmann::json claim_update;
  claim_update["$set"]["consumed"]    = true;
  if (!m_db.update_collection("sso_transactions", claim_filter.dump(),
                              claim_update.dump())) {
    out.error = "transaction already used";
    return out;
  }

  // 3. Exchange the authorization code for tokens. redirect_uri must match
  //    the one sent in begin_login().
  const std::string redirect_uri =
      m_public_base_url + "/api/v1/sso/callback/" + m_config.id;
  const HttpResponse token_resp = m_http.post_form(
      m_endpoints.token_endpoint,
      {{"grant_type", "authorization_code"},
       {"code", code},
       {"redirect_uri", redirect_uri},
       {"client_id", m_config.client_id},
       {"client_secret", m_config.client_secret},
       {"code_verifier", verifier}});
  if (!token_resp.ok()) {
    out.error = "token endpoint returned an error";
    return out;
  }
  nlohmann::json token_json =
      nlohmann::json::parse(token_resp.body, nullptr, false);
  if (token_json.is_discarded() || !token_json.is_object()) {
    out.error = "malformed token response";
    return out;
  }
  const std::string id_token = token_json.value("id_token", std::string{});
  if (id_token.empty()) {
    out.error = "token response carried no id_token";
    return out;
  }

  // 4. Fetch the JWKS and verify the id_token.
  const HttpResponse jwks_resp = m_http.get(m_endpoints.jwks_uri);
  Jwks jwks;
  if (!jwks_resp.ok() || !jwks.parse(jwks_resp.body)) {
    out.error = "could not load the IdP signing keys";
    return out;
  }
  JwtExpect expect;
  expect.issuer    = m_config.issuer;
  expect.client_id = m_config.client_id;
  expect.nonce     = nonce;
  const JwtResult jwt = verify_jwt(id_token, jwks, expect, m_clock.now_unix());
  if (!jwt.ok) {
    out.error = "id_token verification failed: " + jwt.error;
    return out;
  }

  // 5. Map the verified claims.
  out.ok                = true;
  out.subject           = jwt.subject;
  out.email             = jwt.email;
  out.email_verified    = jwt.email_verified;
  out.display_name      = jwt.name;
  out.groups            = jwt.groups;
  out.idp_refresh_token = token_json.value("refresh_token", std::string{});
  return out;
}

} // namespace sso
