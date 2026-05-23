// modules/module/inhouseidp/test/idp_session_test.cc
//
// Tests for IdpSessionManager (Phase D part 1). The IdP-side session
// manager — distinct from the v1.0 sso::SessionManager which handles
// the RP-side xpmile_session cookie.

#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "idp_session.hpp"
#include "json.hpp"
#include "mongodbc.hpp"
#include "sso_session.hpp"  // sso::IClock

namespace {

class TestClock : public sso::IClock {
public:
  std::int64_t now = 1000;
  std::int64_t now_unix() const override { return now; }
};

class MockMongodbClient : public IMongodbClient {
public:
  struct CreateCall { std::string db, coll, doc; };
  struct DeleteCall { std::string coll, doc; };

  std::vector<CreateCall> creates;
  std::vector<DeleteCall> deletes;
  std::string             canned_get_document;
  std::string             database;
  std::string             last_get_document_query;

  const std::string &get_database() const override { return database; }

  std::string create_document(const std::string &db, const std::string &coll,
                               const std::string &doc) override {
    creates.push_back({db, coll, doc});
    return "fake-oid";
  }
  std::string get_document(const std::string &coll, const std::string &query,
                            const std::string &) override {
    (void)coll;
    last_get_document_query = query;
    return canned_get_document;
  }
  bool delete_document(const std::string &coll,
                       const std::string &filter) override {
    deletes.push_back({coll, filter});
    return true;
  }

  // ── stubs (unused by IdpSessionManager) ─────────────────────────────────
  std::int32_t create_bulk_document(const std::string &, const std::string &,
                                    const std::string &) override { return 0; }
  bool update_collection(const std::string &, const std::string &,
                         const std::string &) override { return false; }
  std::int32_t update_bulk_document(const std::string &,
                                    const std::vector<std::string> &,
                                    const std::vector<std::string> &) override { return 0; }
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

TEST(IdpSession, Create_PersistsToIdpSessionsAndReturnsSid) {
  MockMongodbClient db;
  TestClock clock;
  idp::IdpSessionManager sm(db, clock);

  auto sid = sm.create({"alice", "a@x.com", "Alice", "Customer"});

  ASSERT_FALSE(sid.empty());
  ASSERT_EQ(db.creates.size(), 1u);
  EXPECT_EQ(db.creates[0].db,   "idp");
  EXPECT_EQ(db.creates[0].coll, "sessions");
  auto doc = nlohmann::json::parse(db.creates[0].doc);
  EXPECT_EQ(doc["_id"],          sid);
  EXPECT_EQ(doc["accountCode"],  "alice");
  EXPECT_EQ(doc["email"],        "a@x.com");
  EXPECT_EQ(doc["name"],         "Alice");
  EXPECT_EQ(doc["role"],         "Customer");
  EXPECT_EQ(doc["createdAt"].get<std::int64_t>(),  1000);
  EXPECT_EQ(doc["lastSeenAt"].get<std::int64_t>(), 1000);
  EXPECT_EQ(doc["expiresAt"].get<std::int64_t>(),
            1000 + idp::IdpSessionManager::kDefaultMaxAge);
}

TEST(IdpSession, Create_SidIsCspRng_32Bytes_Base64url) {
  MockMongodbClient db;
  TestClock clock;
  idp::IdpSessionManager sm(db, clock);
  auto sid1 = sm.create({"a", "", "", ""});
  auto sid2 = sm.create({"a", "", "", ""});
  ASSERT_FALSE(sid1.empty());
  ASSERT_FALSE(sid2.empty());
  EXPECT_NE(sid1, sid2) << "sids must be CSPRNG-random per call";
  // 32 bytes base64url-encoded (no padding) → 43 chars.
  EXPECT_EQ(sid1.size(), 43u);
}

TEST(IdpSession, Lookup_ValidSid_Returns_AuthContext) {
  MockMongodbClient db;
  TestClock clock;
  clock.now = 5000;
  nlohmann::json stored = {
      {"_id",         "S1"},
      {"accountCode", "alice"},
      {"email",       "a@x.com"},
      {"name",        "Alice"},
      {"role",        "Admin"},
      {"expiresAt",   10000},
  };
  db.canned_get_document = stored.dump();

  idp::IdpSessionManager sm(db, clock);
  auto ctx = sm.lookup("S1");

  EXPECT_TRUE(ctx.valid);
  EXPECT_EQ(ctx.sid,         "S1");
  EXPECT_EQ(ctx.accountCode, "alice");
  EXPECT_EQ(ctx.email,       "a@x.com");
  EXPECT_EQ(ctx.name,        "Alice");
  EXPECT_EQ(ctx.role,        "Admin");
}

TEST(IdpSession, Lookup_UnknownSid_Returns_Invalid) {
  MockMongodbClient db;
  TestClock clock;
  db.canned_get_document = "";  // simulates "not found"
  idp::IdpSessionManager sm(db, clock);
  auto ctx = sm.lookup("nope");
  EXPECT_FALSE(ctx.valid);
}

TEST(IdpSession, Lookup_ExpiredSession_Returns_Invalid) {
  MockMongodbClient db;
  TestClock clock;
  clock.now = 99999;
  nlohmann::json stored = {
      {"_id", "S1"}, {"accountCode", "alice"},
      {"expiresAt", 10000},  // long past
  };
  db.canned_get_document = stored.dump();
  idp::IdpSessionManager sm(db, clock);
  auto ctx = sm.lookup("S1");
  EXPECT_FALSE(ctx.valid);
}

TEST(IdpSession, Lookup_EmptySid_Returns_Invalid_WithoutDbHit) {
  MockMongodbClient db;
  TestClock clock;
  db.canned_get_document = R"({"_id":"oops","accountCode":"a","expiresAt":99999})";
  idp::IdpSessionManager sm(db, clock);
  auto ctx = sm.lookup("");
  EXPECT_FALSE(ctx.valid);
  EXPECT_TRUE(db.last_get_document_query.empty())
      << "empty sid must short-circuit before any DB query";
}

TEST(IdpSession, Revoke_DeletesByIdInIdpSessionsCollection) {
  MockMongodbClient db;
  TestClock clock;
  idp::IdpSessionManager sm(db, clock);
  sm.revoke("S1");
  ASSERT_EQ(db.deletes.size(), 1u);
  EXPECT_EQ(db.deletes[0].coll, "sessions");
  auto filter = nlohmann::json::parse(db.deletes[0].doc);
  EXPECT_EQ(filter["_id"], "S1");
}

TEST(IdpSession, Revoke_EmptySid_NoOp) {
  MockMongodbClient db;
  TestClock clock;
  idp::IdpSessionManager sm(db, clock);
  sm.revoke("");
  EXPECT_TRUE(db.deletes.empty());
}

TEST(IdpSession, RevokeAllForAccount_DeletesByAccountCode) {
  MockMongodbClient db;
  TestClock clock;
  idp::IdpSessionManager sm(db, clock);
  sm.revoke_all_for_account("alice");
  ASSERT_EQ(db.deletes.size(), 1u);
  EXPECT_EQ(db.deletes[0].coll, "sessions");
  auto filter = nlohmann::json::parse(db.deletes[0].doc);
  EXPECT_EQ(filter["accountCode"], "alice");
}
