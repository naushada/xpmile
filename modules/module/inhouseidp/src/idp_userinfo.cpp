#include "idp_userinfo.hpp"

#include <cstdint>
#include <string>

#include "json.hpp"
#include "mongodbc.hpp"

namespace idp {

namespace {

sso::SsoHttpResult unauthorized(const std::string &reason) {
  sso::SsoHttpResult r;
  r.status       = 401;
  r.content_type = "application/json";
  // OIDC §5.3 / RFC 6750: WWW-Authenticate is conventional; the SsoHttpResult
  // doesn't have a generic header field beyond Set-Cookie/Location, so we
  // surface the reason via the body. The wire adapter can add the header.
  nlohmann::json body = {{"error", "invalid_token"}, {"error_description", reason}};
  r.body = body.dump();
  return r;
}

/// Extract the bearer token from `Authorization: Bearer <token>`. Returns ""
/// on malformed input. Case-insensitive on the scheme name.
std::string extract_bearer(const std::string &header) {
  if (header.size() < 7) return {};
  const std::string scheme = header.substr(0, 6);
  // ASCII case-fold the scheme.
  std::string lower = scheme;
  for (char &c : lower) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
  }
  if (lower != "bearer") return {};
  // Skip whitespace after "Bearer".
  std::size_t pos = 6;
  while (pos < header.size() && (header[pos] == ' ' || header[pos] == '\t')) ++pos;
  return header.substr(pos);
}

} // namespace

sso::SsoHttpResult userinfo(const std::string &authorization_header,
                              IMongodbClient &db,
                              sso::IClock &clock) {
  const std::string token = extract_bearer(authorization_header);
  if (token.empty()) {
    return unauthorized("missing or malformed Authorization: Bearer ...");
  }

  const nlohmann::json query = {{"_id", token}};
  const std::string raw =
      db.get_document("idp_access_tokens", query.dump(), "{}");
  if (raw.empty()) {
    return unauthorized("unknown access_token");
  }

  nlohmann::json doc;
  try {
    doc = nlohmann::json::parse(raw);
  } catch (const std::exception &) {
    return unauthorized("malformed access_token record");
  }

  const std::int64_t now = clock.now_unix();
  if (doc.value("expiresAt", std::int64_t{0}) <= now) {
    return unauthorized("access_token expired");
  }

  nlohmann::json claims = {
      {"sub",   doc.value("sub",   std::string{})},
  };
  if (auto v = doc.value("email", std::string{}); !v.empty()) claims["email"] = v;
  if (auto v = doc.value("name",  std::string{}); !v.empty()) claims["name"]  = v;
  if (auto v = doc.value("role",  std::string{}); !v.empty()) claims["role"]  = v;

  sso::SsoHttpResult r;
  r.status       = 200;
  r.content_type = "application/json";
  r.body         = claims.dump();
  return r;
}

} // namespace idp
