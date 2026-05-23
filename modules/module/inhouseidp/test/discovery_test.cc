// modules/module/inhouseidp/test/discovery_test.cc
//
// Tests for build_discovery + handle_idp_discovery_GET (Phase C).

#include <string>

#include <gtest/gtest.h>

#include "idp_discovery.hpp"
#include "json.hpp"

namespace {

constexpr const char *kIssuer =
    "https://idp-63c97365e6ef.herokuapp.com/api/v1/idp";

nlohmann::json discovery_of(const std::string &issuer) {
  return nlohmann::json::parse(idp::build_discovery(issuer));
}

bool is_absolute_https(const std::string &url) {
  return url.rfind("https://", 0) == 0;
}

} // namespace

// ── build_discovery ──────────────────────────────────────────────────────────

TEST(IdpDiscovery, IssuerMatchesConfig) {
  auto j = discovery_of(kIssuer);
  EXPECT_EQ(j["issuer"], kIssuer);
}

TEST(IdpDiscovery, EndpointsAreDerivedFromIssuer) {
  auto j = discovery_of(kIssuer);
  EXPECT_EQ(j["authorization_endpoint"], std::string(kIssuer) + "/authorize");
  EXPECT_EQ(j["token_endpoint"],         std::string(kIssuer) + "/token");
  EXPECT_EQ(j["jwks_uri"],               std::string(kIssuer) + "/jwks");
  EXPECT_EQ(j["userinfo_endpoint"],      std::string(kIssuer) + "/userinfo");
  EXPECT_EQ(j["end_session_endpoint"],   std::string(kIssuer) + "/end_session");
}

TEST(IdpDiscovery, AdvertisesPKCEMethodS256Only) {
  auto j = discovery_of(kIssuer);
  ASSERT_EQ(j["code_challenge_methods_supported"].size(), 1u);
  EXPECT_EQ(j["code_challenge_methods_supported"][0], "S256");
}

TEST(IdpDiscovery, AdvertisesRS256Only) {
  auto j = discovery_of(kIssuer);
  ASSERT_EQ(j["id_token_signing_alg_values_supported"].size(), 1u);
  EXPECT_EQ(j["id_token_signing_alg_values_supported"][0], "RS256");
}

TEST(IdpDiscovery, AdvertisesResponseTypeCodeOnly) {
  auto j = discovery_of(kIssuer);
  ASSERT_EQ(j["response_types_supported"].size(), 1u);
  EXPECT_EQ(j["response_types_supported"][0], "code");
}

TEST(IdpDiscovery, IncludesScopesAndClaims) {
  auto j = discovery_of(kIssuer);
  const auto scopes = j["scopes_supported"].get<std::vector<std::string>>();
  EXPECT_NE(std::find(scopes.begin(), scopes.end(), "openid"), scopes.end());
  EXPECT_NE(std::find(scopes.begin(), scopes.end(), "email"),  scopes.end());
  EXPECT_NE(std::find(scopes.begin(), scopes.end(), "profile"), scopes.end());

  const auto claims = j["claims_supported"].get<std::vector<std::string>>();
  EXPECT_NE(std::find(claims.begin(), claims.end(), "sub"),    claims.end());
  EXPECT_NE(std::find(claims.begin(), claims.end(), "iss"),    claims.end());
  EXPECT_NE(std::find(claims.begin(), claims.end(), "aud"),    claims.end());
  EXPECT_NE(std::find(claims.begin(), claims.end(), "exp"),    claims.end());
  EXPECT_NE(std::find(claims.begin(), claims.end(), "iat"),    claims.end());
  EXPECT_NE(std::find(claims.begin(), claims.end(), "nonce"),  claims.end());
  EXPECT_NE(std::find(claims.begin(), claims.end(), "email"),  claims.end());
  EXPECT_NE(std::find(claims.begin(), claims.end(), "name"),   claims.end());
  EXPECT_NE(std::find(claims.begin(), claims.end(), "role"),   claims.end());
}

TEST(IdpDiscovery, AdvertisesClientSecretPostOnly) {
  auto j = discovery_of(kIssuer);
  ASSERT_EQ(j["token_endpoint_auth_methods_supported"].size(), 1u);
  EXPECT_EQ(j["token_endpoint_auth_methods_supported"][0], "client_secret_post");
}

TEST(IdpDiscovery, IssuerWithTrailingSlash_IsNormalized) {
  auto j = discovery_of(std::string(kIssuer) + "/");
  EXPECT_EQ(j["issuer"], kIssuer);
  EXPECT_EQ(j["jwks_uri"], std::string(kIssuer) + "/jwks");
}

// ── handle_idp_discovery_GET ─────────────────────────────────────────────────

TEST(IdpDiscoveryEndpoint, ReturnsJsonContentType_And200) {
  auto r = idp::handle_idp_discovery_GET(kIssuer);
  EXPECT_EQ(r.status, 200);
  EXPECT_EQ(r.content_type, "application/json");
}

TEST(IdpDiscoveryEndpoint, AllUrlsAreAbsolute) {
  auto r = idp::handle_idp_discovery_GET(kIssuer);
  auto j = nlohmann::json::parse(r.body);
  for (const char *field : {
           "issuer", "authorization_endpoint", "token_endpoint",
           "jwks_uri", "userinfo_endpoint", "end_session_endpoint"}) {
    ASSERT_TRUE(j.contains(field));
    EXPECT_TRUE(is_absolute_https(j[field].get<std::string>()))
        << field << " is not an absolute https:// URL: " << j[field];
  }
}
