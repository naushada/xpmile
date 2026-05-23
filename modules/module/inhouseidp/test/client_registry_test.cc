// modules/module/inhouseidp/test/client_registry_test.cc
//
// Tests for IdpClientRegistry (Phase D part 1).
// Headline guarantee: redirect-URI matching is EXACT BYTE EQUALITY — no
// prefix, no trailing-slash, no query-string, no case-folding.

#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "idp_client_registry.hpp"
#include "json.hpp"
#include "mongodbc.hpp"

namespace {

class MockMongodbClient : public IMongodbClient {
public:
  std::string canned_get_documents;
  std::string database;

  const std::string &get_database() const override { return database; }
  std::string get_documents(const std::string &, const std::string &,
                             const std::string &) override {
    return canned_get_documents;
  }

  // ── stubs ───────────────────────────────────────────────────────────────
  std::string create_document(const std::string &, const std::string &,
                              const std::string &) override { return {}; }
  std::int32_t create_bulk_document(const std::string &, const std::string &,
                                    const std::string &) override { return 0; }
  bool update_collection(const std::string &, const std::string &,
                         const std::string &) override { return false; }
  std::int32_t update_bulk_document(const std::string &,
                                    const std::vector<std::string> &,
                                    const std::vector<std::string> &) override { return 0; }
  bool delete_document(const std::string &, const std::string &) override { return false; }
  std::string get_document(const std::string &, const std::string &,
                            const std::string &) override { return {}; }
  std::string get_documents(const std::string &, const std::string &) override { return {}; }
  std::string next_awbno(const std::string & = "AWB") override { return {}; }
  std::string store_file(const std::string &, const std::string &,
                          const std::vector<std::uint8_t> &) override { return {}; }
  std::vector<std::uint8_t> fetch_file(const std::string &) override { return {}; }
  std::vector<std::uint8_t> fetch_file_by_id(const std::string &) override { return {}; }
  bool delete_file(const std::string &) override { return false; }
};

nlohmann::json make_client(const std::string &id,
                            const std::vector<std::string> &redirect_uris,
                            const std::vector<std::string> &post_logout = {}) {
  return {
      {"_id",                    id},
      {"clientName",             id + "-name"},
      {"clientSecretHash",       "$2a$ignored"},
      {"redirectUris",           redirect_uris},
      {"postLogoutRedirectUris", post_logout},
      {"scopes",                 {"openid", "email"}},
      {"grantTypes",             {"authorization_code"}},
  };
}

// Seed the mock with an array of client docs and reload.
idp::IdpClientRegistry seeded_with(MockMongodbClient &db,
                                     const std::vector<nlohmann::json> &clients) {
  nlohmann::json arr = nlohmann::json::array();
  for (const auto &c : clients) arr.push_back(c);
  db.canned_get_documents = arr.dump();
  idp::IdpClientRegistry reg;
  reg.reload(db);
  return reg;
}

} // namespace

TEST(IdpClientRegistry, EmptyCollection_LoadsZeroClients) {
  MockMongodbClient db;
  db.canned_get_documents = "[]";
  idp::IdpClientRegistry reg;
  EXPECT_EQ(reg.reload(db), 0);
  EXPECT_EQ(reg.size(), 0u);
}

TEST(IdpClientRegistry, EmptyResponse_LoadsZeroClients) {
  MockMongodbClient db;
  db.canned_get_documents = "";  // no documents at all
  idp::IdpClientRegistry reg;
  EXPECT_EQ(reg.reload(db), 0);
}

TEST(IdpClientRegistry, MalformedJsonResponse_ReturnsZeroNoThrow) {
  MockMongodbClient db;
  db.canned_get_documents = "{not-json";
  idp::IdpClientRegistry reg;
  EXPECT_EQ(reg.reload(db), 0);
}

TEST(IdpClientRegistry, LoadsMultipleClients) {
  MockMongodbClient db;
  auto reg = seeded_with(db, {
      make_client("xpmile-spa", {"https://marvel.app/cb"}),
      make_client("partner-a",  {"https://partner.com/cb"}),
  });
  EXPECT_EQ(reg.size(), 2u);
}

TEST(IdpClientRegistry, FindByClientId_ReturnsClient) {
  MockMongodbClient db;
  auto reg = seeded_with(db, {
      make_client("xpmile-spa", {"https://marvel.app/cb"}),
  });
  const auto *c = reg.find("xpmile-spa");
  ASSERT_NE(c, nullptr);
  EXPECT_EQ(c->clientId,   "xpmile-spa");
  EXPECT_EQ(c->clientName, "xpmile-spa-name");
  ASSERT_EQ(c->redirectUris.size(), 1u);
  EXPECT_EQ(c->redirectUris[0], "https://marvel.app/cb");
}

TEST(IdpClientRegistry, FindByClientId_UnknownReturnsNull) {
  MockMongodbClient db;
  auto reg = seeded_with(db, {make_client("c1", {"https://x.com"})});
  EXPECT_EQ(reg.find("does-not-exist"), nullptr);
}

TEST(IdpClientRegistry, Reload_ReplacesPreviousContents) {
  MockMongodbClient db;
  auto reg = seeded_with(db, {make_client("c1", {"https://x.com"})});
  EXPECT_EQ(reg.size(), 1u);

  // Reload with a different set
  nlohmann::json arr = nlohmann::json::array();
  arr.push_back(make_client("c2", {"https://y.com"}));
  arr.push_back(make_client("c3", {"https://z.com"}));
  db.canned_get_documents = arr.dump();
  reg.reload(db);

  EXPECT_EQ(reg.size(), 2u);
  EXPECT_EQ(reg.find("c1"), nullptr);
  EXPECT_NE(reg.find("c2"), nullptr);
}

// ── redirect_uri validation: exact byte equality ────────────────────────────

TEST(IdpClientRegistry, ValidateRedirectUri_ExactMatchAccepts) {
  MockMongodbClient db;
  auto reg = seeded_with(db, {
      make_client("c1", {"https://app.example/cb"}),
  });
  EXPECT_TRUE(reg.validate_redirect_uri("c1", "https://app.example/cb"));
}

TEST(IdpClientRegistry, ValidateRedirectUri_RejectsTrailingSlash) {
  MockMongodbClient db;
  auto reg = seeded_with(db, {
      make_client("c1", {"https://app.example/cb"}),
  });
  EXPECT_FALSE(reg.validate_redirect_uri("c1", "https://app.example/cb/"));
}

TEST(IdpClientRegistry, ValidateRedirectUri_RejectsQueryString) {
  MockMongodbClient db;
  auto reg = seeded_with(db, {
      make_client("c1", {"https://app.example/cb"}),
  });
  EXPECT_FALSE(reg.validate_redirect_uri("c1", "https://app.example/cb?x=1"));
}

TEST(IdpClientRegistry, ValidateRedirectUri_RejectsPrefix) {
  MockMongodbClient db;
  auto reg = seeded_with(db, {
      make_client("c1", {"https://app.example/cb"}),
  });
  EXPECT_FALSE(reg.validate_redirect_uri("c1", "https://app.example/cbx"));
}

TEST(IdpClientRegistry, ValidateRedirectUri_RejectsCaseMismatch) {
  MockMongodbClient db;
  auto reg = seeded_with(db, {
      make_client("c1", {"https://app.example/cb"}),
  });
  // RFC 3986 says scheme + host are case-insensitive; we're stricter
  // (byte-equality) so the operator's registered URI is the canonical form.
  EXPECT_FALSE(reg.validate_redirect_uri("c1", "https://APP.EXAMPLE/cb"));
}

TEST(IdpClientRegistry, ValidateRedirectUri_RejectsEmpty) {
  MockMongodbClient db;
  auto reg = seeded_with(db, {
      make_client("c1", {"https://app.example/cb"}),
  });
  EXPECT_FALSE(reg.validate_redirect_uri("c1", ""));
}

TEST(IdpClientRegistry, ValidateRedirectUri_UnknownClient_Rejects) {
  MockMongodbClient db;
  db.canned_get_documents = "[]";
  idp::IdpClientRegistry reg;
  reg.reload(db);
  EXPECT_FALSE(reg.validate_redirect_uri("unknown", "https://x.com"));
}

TEST(IdpClientRegistry, ValidateRedirectUri_AcceptsAnyRegisteredInList) {
  MockMongodbClient db;
  auto reg = seeded_with(db, {
      make_client("c1", {
          "https://a.example/cb",
          "https://b.example/cb",
      }),
  });
  EXPECT_TRUE(reg.validate_redirect_uri("c1", "https://a.example/cb"));
  EXPECT_TRUE(reg.validate_redirect_uri("c1", "https://b.example/cb"));
  EXPECT_FALSE(reg.validate_redirect_uri("c1", "https://c.example/cb"));
}

// ── post_logout_redirect_uri validation ─────────────────────────────────────

TEST(IdpClientRegistry, ValidatePostLogoutRedirectUri_ExactMatch) {
  MockMongodbClient db;
  auto reg = seeded_with(db, {
      make_client("c1", {"https://a/cb"}, {"https://a/post-logout"}),
  });
  EXPECT_TRUE(reg.validate_post_logout_redirect_uri("c1", "https://a/post-logout"));
  EXPECT_FALSE(reg.validate_post_logout_redirect_uri("c1", "https://a/different"));
}

TEST(IdpClientRegistry, ValidatePostLogoutRedirectUri_EmptyListRejects) {
  MockMongodbClient db;
  auto reg = seeded_with(db, {
      make_client("c1", {"https://a/cb"}, /*post_logout=*/{}),
  });
  EXPECT_FALSE(reg.validate_post_logout_redirect_uri("c1", "https://a/anything"));
}
