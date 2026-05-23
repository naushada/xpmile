#include "idp_discovery.hpp"

#include "json.hpp"

namespace idp {

namespace {

// Strip any trailing slash from the issuer so URL concatenation below is
// always single-slash.
std::string normalize_issuer(const std::string &issuer) {
  if (!issuer.empty() && issuer.back() == '/') {
    return issuer.substr(0, issuer.size() - 1);
  }
  return issuer;
}

} // namespace

std::string build_discovery(const std::string &issuer) {
  const std::string base = normalize_issuer(issuer);

  nlohmann::json doc = {
      {"issuer",                                base},
      {"authorization_endpoint",                base + "/authorize"},
      {"token_endpoint",                        base + "/token"},
      {"jwks_uri",                              base + "/jwks"},
      {"userinfo_endpoint",                     base + "/userinfo"},
      {"end_session_endpoint",                  base + "/end_session"},
      {"response_types_supported",              nlohmann::json::array({"code"})},
      {"subject_types_supported",               nlohmann::json::array({"public"})},
      {"id_token_signing_alg_values_supported", nlohmann::json::array({"RS256"})},
      {"token_endpoint_auth_methods_supported", nlohmann::json::array({"client_secret_post"})},
      {"scopes_supported",
       nlohmann::json::array({"openid", "email", "profile"})},
      {"claims_supported",
       nlohmann::json::array({"sub", "iss", "aud", "exp", "iat", "nonce",
                              "email", "name", "role"})},
      {"code_challenge_methods_supported",      nlohmann::json::array({"S256"})},
  };

  return doc.dump();
}

sso::SsoHttpResult handle_idp_discovery_GET(const std::string &issuer) {
  sso::SsoHttpResult r;
  r.status       = 200;
  r.content_type = "application/json";
  r.body         = build_discovery(issuer);
  return r;
}

} // namespace idp
