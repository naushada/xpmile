// modules/module/inhouseidp/test/userinfo_test.cc

#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "idp_userinfo.hpp"
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
  std::string canned_get_document;
  std::string database;
  const std::string &get_database() const override { return database; }
  std::string get_document(const std::string &, const std::string &,
                            const std::string &) override {
    return canned_get_document;
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
  std::string get_documents(const std::string &, const std::string &,
                             const std::string &) override { return {}; }
  std::string get_documents(const std::string &, const std::string &) override { return {}; }
  std::string next_awbno(const std::string & = "AWB") override { return {}; }
  std::string store_file(const std::string &, const std::string &,
                          const std::vector<std::uint8_t> &) override { return {}; }
  std::vector<std::uint8_t> fetch_file(const std::string &) override { return {}; }
  std::vector<std::uint8_t> fetch_file_by_id(const std::string &) override { return {}; }
  bool delete_file(const std::string &) override { return false; }
};

} // namespace

TEST(IdpUserinfo, ValidBearer_Returns_Claims) {
  MockMongodbClient db;
  TestClock clock; clock.now = 1000;
  nlohmann::json doc = {
      {"_id",       "AT1"},
      {"client_id", "xpmile-spa"},
      {"sub",       "alice"},
      {"email",     "a@x.com"},
      {"name",      "Alice"},
      {"role",      "Admin"},
      {"expiresAt", 10000},
  };
  db.canned_get_document = doc.dump();

  auto r = idp::userinfo("Bearer AT1", db, clock);
  ASSERT_EQ(r.status, 200);
  auto j = nlohmann::json::parse(r.body);
  EXPECT_EQ(j["sub"],   "alice");
  EXPECT_EQ(j["email"], "a@x.com");
  EXPECT_EQ(j["name"],  "Alice");
  EXPECT_EQ(j["role"],  "Admin");
}

TEST(IdpUserinfo, MissingAuthorizationHeader_Returns_401) {
  MockMongodbClient db; TestClock clock;
  auto r = idp::userinfo("", db, clock);
  EXPECT_EQ(r.status, 401);
}

TEST(IdpUserinfo, MalformedAuthorizationHeader_Returns_401) {
  MockMongodbClient db; TestClock clock;
  // Not "Bearer ..."
  auto r = idp::userinfo("Basic abc:def", db, clock);
  EXPECT_EQ(r.status, 401);
}

TEST(IdpUserinfo, UnknownAccessToken_Returns_401) {
  MockMongodbClient db; TestClock clock;
  db.canned_get_document = "";  // not found
  auto r = idp::userinfo("Bearer UNKNOWN", db, clock);
  EXPECT_EQ(r.status, 401);
}

TEST(IdpUserinfo, ExpiredAccessToken_Returns_401) {
  MockMongodbClient db; TestClock clock; clock.now = 99999;
  nlohmann::json doc = {
      {"_id", "AT1"}, {"sub", "alice"}, {"expiresAt", 500},  // past
  };
  db.canned_get_document = doc.dump();
  auto r = idp::userinfo("Bearer AT1", db, clock);
  EXPECT_EQ(r.status, 401);
}

TEST(IdpUserinfo, BearerSchemeIsCaseInsensitive) {
  MockMongodbClient db; TestClock clock;
  nlohmann::json doc = {{"_id","AT1"},{"sub","alice"},{"expiresAt",99999}};
  db.canned_get_document = doc.dump();
  EXPECT_EQ(idp::userinfo("bearer AT1", db, clock).status, 200);
  EXPECT_EQ(idp::userinfo("BEARER AT1", db, clock).status, 200);
}
