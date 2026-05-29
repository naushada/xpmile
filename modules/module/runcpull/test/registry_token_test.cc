// Phase B.2 — Token flow: Www-Authenticate + token URL + /token response.
// 9 tests.

#include <gtest/gtest.h>

#include "registry_client.hpp"

using runcpull::AuthChallengeParse;
using runcpull::build_token_url;
using runcpull::parse_token_response;
using runcpull::parse_www_authenticate;

// ── parse_www_authenticate ────────────────────────────────────────────────

TEST(RegistryTokenTest, ParseWwwAuthenticate_ExtractsRealmAndService) {
  auto p = parse_www_authenticate(
      R"(Bearer realm="https://auth.docker.io/token",service="registry.docker.io")");
  ASSERT_TRUE(p.ok);
  EXPECT_EQ(p.value.realm,   "https://auth.docker.io/token");
  EXPECT_EQ(p.value.service, "registry.docker.io");
}

TEST(RegistryTokenTest, ParseWwwAuthenticate_HandlesQuotedAndUnquoted) {
  // Quoted realm + unquoted service.
  auto p = parse_www_authenticate(
      R"(Bearer realm="https://auth.example/token",service=example.io)");
  ASSERT_TRUE(p.ok);
  EXPECT_EQ(p.value.realm,   "https://auth.example/token");
  EXPECT_EQ(p.value.service, "example.io");
}

TEST(RegistryTokenTest, ParseWwwAuthenticate_MissingRealm_Errors) {
  auto p = parse_www_authenticate(
      R"(Bearer service="registry.docker.io")");
  EXPECT_FALSE(p.ok);
}

// ── build_token_url ──────────────────────────────────────────────────────

TEST(RegistryTokenTest, BuildTokenUrl_EncodesScope) {
  const std::string u = build_token_url(
      "https://auth.docker.io/token",
      "registry.docker.io",
      "naushada/foo",
      "pull");
  EXPECT_NE(u.find("service="), std::string::npos);
  EXPECT_NE(u.find("scope="), std::string::npos);
  // The scope payload "repository:naushada/foo:pull" should be url-encoded
  // — at minimum the colons should appear as %3A.
  EXPECT_NE(u.find("%3A"), std::string::npos)
      << "scope colons should be percent-encoded";
  // And the slash in the image name → %2F.
  EXPECT_NE(u.find("%2F"), std::string::npos)
      << "image-name slash should be percent-encoded";
}

// ── parse_token_response ─────────────────────────────────────────────────

TEST(RegistryTokenTest, ParseTokenResponse_TokenField) {
  auto t = parse_token_response(R"({"token":"abc.def.ghi"})");
  ASSERT_TRUE(t.ok);
  EXPECT_EQ(t.token, "abc.def.ghi");
}

TEST(RegistryTokenTest, ParseTokenResponse_AccessTokenField) {
  auto t = parse_token_response(R"({"access_token":"xyz"})");
  ASSERT_TRUE(t.ok);
  EXPECT_EQ(t.token, "xyz");
}

TEST(RegistryTokenTest, ParseTokenResponse_PrefersToken_WhenBothPresent) {
  auto t = parse_token_response(
      R"({"token":"prefer-this","access_token":"not-this"})");
  ASSERT_TRUE(t.ok);
  EXPECT_EQ(t.token, "prefer-this");
}

TEST(RegistryTokenTest, ParseTokenResponse_MissingBoth_Errors) {
  auto t = parse_token_response(R"({"expires_in":300})");
  EXPECT_FALSE(t.ok);
  EXPECT_TRUE(t.token.empty());
}

TEST(RegistryTokenTest, ParseTokenResponse_MalformedJson_Errors) {
  auto t = parse_token_response("not valid json at all");
  EXPECT_FALSE(t.ok);
}
