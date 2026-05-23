// modules/module/inhouseidp/test/end_session_test.cc

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "idp_client_registry.hpp"
#include "idp_cookie.hpp"
#include "idp_end_session.hpp"
#include "idp_session.hpp"
#include "json.hpp"
#include "mongodbc.hpp"
#include "sso_session.hpp"

namespace {

class TestClock : public sso::IClock {
public:
  std::int64_t now = 1000;
  std::int64_t now_unix() const override { return now; }
};

class MockMongodbClient : public IMongodbClient {
public:
  struct DeleteCall { std::string coll, doc; };
  std::vector<DeleteCall> deletes;

  std::string canned_get_documents;
  std::string database;

  const std::string &get_database() const override { return database; }
  std::string get_documents(const std::string &, const std::string &,
                             const std::string &) override {
    return canned_get_documents;
  }
  bool delete_document(const std::string &coll, const std::string &doc) override {
    deletes.push_back({coll, doc});
    return true;
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

idp::IdpClientRegistry registry_with_post_logout(MockMongodbClient &db,
                                                    const std::string &uri) {
  nlohmann::json arr = nlohmann::json::array();
  arr.push_back({
      {"_id",                    "xpmile-spa"},
      {"clientName",             "xpmile SPA"},
      {"redirectUris",           {"https://marvel.app/cb"}},
      {"postLogoutRedirectUris", {uri}},
  });
  db.canned_get_documents = arr.dump();
  idp::IdpClientRegistry reg;
  reg.reload(db);
  return reg;
}

} // namespace

TEST(IdpEndSession, DeletesIdpSession_AndRedirectsTo_PostLogout) {
  MockMongodbClient db;
  TestClock clock;
  auto reg = registry_with_post_logout(db, "https://marvel.app/login");
  idp::IdpSessionManager sm(db, clock);

  std::map<std::string, std::string> query = {
      {"client_id",                "xpmile-spa"},
      {"post_logout_redirect_uri", "https://marvel.app/login"},
  };
  auto r = idp::end_session(query, "xpmile_idp_session=SID1",
                              db, reg, sm);
  EXPECT_EQ(r.status, 302);
  EXPECT_EQ(r.location, "https://marvel.app/login");
  EXPECT_NE(r.set_cookie.find("xpmile_idp_session="), std::string::npos);
  EXPECT_NE(r.set_cookie.find("Max-Age=0"),           std::string::npos);

  // Session row was deleted.
  ASSERT_EQ(db.deletes.size(), 1u);
  EXPECT_EQ(db.deletes[0].coll, "sessions");
}

TEST(IdpEndSession, UnregisteredPostLogoutUri_Returns_400) {
  MockMongodbClient db;
  TestClock clock;
  auto reg = registry_with_post_logout(db, "https://marvel.app/login");
  idp::IdpSessionManager sm(db, clock);

  std::map<std::string, std::string> query = {
      {"client_id",                "xpmile-spa"},
      {"post_logout_redirect_uri", "https://attacker.example/grab-cookies"},
  };
  auto r = idp::end_session(query, "xpmile_idp_session=SID1",
                              db, reg, sm);
  EXPECT_EQ(r.status, 400);
}

TEST(IdpEndSession, NoCookie_StillReturns_302_AndExpiresCookie) {
  MockMongodbClient db;
  TestClock clock;
  auto reg = registry_with_post_logout(db, "https://marvel.app/login");
  idp::IdpSessionManager sm(db, clock);

  std::map<std::string, std::string> query = {
      {"client_id",                "xpmile-spa"},
      {"post_logout_redirect_uri", "https://marvel.app/login"},
  };
  auto r = idp::end_session(query, /*cookie_header=*/"", db, reg, sm);
  EXPECT_EQ(r.status, 302);
  EXPECT_EQ(r.location, "https://marvel.app/login");
  EXPECT_NE(r.set_cookie.find("Max-Age=0"), std::string::npos);
  // No session row to delete.
  EXPECT_TRUE(db.deletes.empty());
}

TEST(IdpEndSession, NoPostLogoutUri_DefaultsTo_Login) {
  MockMongodbClient db;
  TestClock clock;
  auto reg = registry_with_post_logout(db, "https://marvel.app/login");
  idp::IdpSessionManager sm(db, clock);

  auto r = idp::end_session({}, "xpmile_idp_session=SID1", db, reg, sm);
  EXPECT_EQ(r.status, 302);
  EXPECT_EQ(r.location, "/idp/login");
}

TEST(IdpEndSession, PostLogoutWithoutClientId_Returns_400) {
  MockMongodbClient db;
  TestClock clock;
  auto reg = registry_with_post_logout(db, "https://marvel.app/login");
  idp::IdpSessionManager sm(db, clock);

  std::map<std::string, std::string> query = {
      {"post_logout_redirect_uri", "https://marvel.app/login"},
      // client_id missing
  };
  auto r = idp::end_session(query, "xpmile_idp_session=SID1",
                              db, reg, sm);
  EXPECT_EQ(r.status, 400);
}
