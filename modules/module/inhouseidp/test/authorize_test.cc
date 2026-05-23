// modules/module/inhouseidp/test/authorize_test.cc
//
// Tests for the /authorize endpoint logic (Phase D part 2).

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "idp_authorize.hpp"
#include "idp_client_registry.hpp"
#include "idp_cookie.hpp"
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

/// Mock that records creates and serves canned get_document responses
/// for both the session lookup (kSessions key) and the (unused here)
/// pending_auth / codes lookups.
class MockMongodbClient : public IMongodbClient {
public:
  struct CreateCall { std::string db, coll, doc; };
  std::vector<CreateCall> creates;

  // Canned response for get_document — keyed off the collection so that
  // session lookup vs pending_auth lookup can be set independently.
  std::map<std::string, std::string> canned_get_document;
  // Canned response for get_documents (used by IdpClientRegistry::reload).
  std::string canned_get_documents;
  std::string database;

  const std::string &get_database() const override { return database; }
  std::string create_document(const std::string &db, const std::string &coll,
                               const std::string &doc) override {
    creates.push_back({db, coll, doc});
    return "fake-oid";
  }
  std::string get_document(const std::string &coll, const std::string &,
                            const std::string &) override {
    auto it = canned_get_document.find(coll);
    return (it == canned_get_document.end()) ? std::string{} : it->second;
  }
  std::string get_documents(const std::string &, const std::string &,
                             const std::string &) override {
    return canned_get_documents;
  }

  // ── stubs ───────────────────────────────────────────────────────────────
  std::int32_t create_bulk_document(const std::string &, const std::string &,
                                    const std::string &) override { return 0; }
  bool update_collection(const std::string &, const std::string &,
                         const std::string &) override { return false; }
  std::int32_t update_bulk_document(const std::string &,
                                    const std::vector<std::string> &,
                                    const std::vector<std::string> &) override { return 0; }
  bool delete_document(const std::string &, const std::string &) override { return false; }
  std::string get_documents(const std::string &, const std::string &) override { return {}; }
  std::string next_awbno(const std::string & = "AWB") override { return {}; }
  std::string store_file(const std::string &, const std::string &,
                          const std::vector<std::uint8_t> &) override { return {}; }
  std::vector<std::uint8_t> fetch_file(const std::string &) override { return {}; }
  std::vector<std::uint8_t> fetch_file_by_id(const std::string &) override { return {}; }
  bool delete_file(const std::string &) override { return false; }
};

idp::IdpClientRegistry registry_with_xpmile_spa(MockMongodbClient &db) {
  nlohmann::json arr = nlohmann::json::array();
  arr.push_back({
      {"_id",                    "xpmile-spa"},
      {"clientName",             "xpmile SPA"},
      {"redirectUris",           {"https://marvel.app/cb"}},
      {"postLogoutRedirectUris", {"https://marvel.app/login"}},
      {"scopes",                 {"openid", "email", "profile"}},
      {"grantTypes",             {"authorization_code"}},
  });
  db.canned_get_documents = arr.dump();
  idp::IdpClientRegistry reg;
  reg.reload(db);
  return reg;
}

std::map<std::string, std::string> valid_query() {
  return {
      {"response_type",         "code"},
      {"client_id",             "xpmile-spa"},
      {"redirect_uri",          "https://marvel.app/cb"},
      {"scope",                 "openid email profile"},
      {"state",                 "RP-STATE-1"},
      {"nonce",                 "RP-NONCE-1"},
      {"code_challenge",        "PKCE-CHALLENGE-1"},
      {"code_challenge_method", "S256"},
  };
}

} // namespace

// ── parameter validation ─────────────────────────────────────────────────────

TEST(IdpAuthorize, RejectsMissingClientId) {
  MockMongodbClient db;
  auto reg = registry_with_xpmile_spa(db);
  TestClock clock;
  idp::IdpSessionManager sm(db, clock);

  auto q = valid_query();
  q.erase("client_id");
  auto r = idp::authorize(q, "", db, reg, sm, clock);
  EXPECT_EQ(r.status, 400);
}

TEST(IdpAuthorize, RejectsUnknownClientId) {
  MockMongodbClient db;
  auto reg = registry_with_xpmile_spa(db);
  TestClock clock;
  idp::IdpSessionManager sm(db, clock);
  auto q = valid_query();
  q["client_id"] = "unknown";
  auto r = idp::authorize(q, "", db, reg, sm, clock);
  EXPECT_EQ(r.status, 400);
  EXPECT_NE(r.body.find("invalid_client"), std::string::npos);
}

TEST(IdpAuthorize, RejectsUnregisteredRedirectUri) {
  MockMongodbClient db;
  auto reg = registry_with_xpmile_spa(db);
  TestClock clock;
  idp::IdpSessionManager sm(db, clock);
  auto q = valid_query();
  q["redirect_uri"] = "https://evil.com/cb";
  auto r = idp::authorize(q, "", db, reg, sm, clock);
  EXPECT_EQ(r.status, 400);
}

TEST(IdpAuthorize, RejectsResponseTypeOtherThanCode) {
  MockMongodbClient db;
  auto reg = registry_with_xpmile_spa(db);
  TestClock clock;
  idp::IdpSessionManager sm(db, clock);
  auto q = valid_query();
  q["response_type"] = "token";
  auto r = idp::authorize(q, "", db, reg, sm, clock);
  EXPECT_EQ(r.status, 400);
  EXPECT_NE(r.body.find("unsupported_response_type"), std::string::npos);
}

TEST(IdpAuthorize, RejectsMissingPKCEChallenge) {
  MockMongodbClient db;
  auto reg = registry_with_xpmile_spa(db);
  TestClock clock;
  idp::IdpSessionManager sm(db, clock);
  auto q = valid_query();
  q["code_challenge"] = "";
  auto r = idp::authorize(q, "", db, reg, sm, clock);
  EXPECT_EQ(r.status, 400);
}

TEST(IdpAuthorize, RejectsCodeChallengeMethodOtherThanS256) {
  MockMongodbClient db;
  auto reg = registry_with_xpmile_spa(db);
  TestClock clock;
  idp::IdpSessionManager sm(db, clock);
  auto q = valid_query();
  q["code_challenge_method"] = "plain";
  auto r = idp::authorize(q, "", db, reg, sm, clock);
  EXPECT_EQ(r.status, 400);
}

TEST(IdpAuthorize, RejectsMissingOpenidScope) {
  MockMongodbClient db;
  auto reg = registry_with_xpmile_spa(db);
  TestClock clock;
  idp::IdpSessionManager sm(db, clock);
  auto q = valid_query();
  q["scope"] = "email profile";
  auto r = idp::authorize(q, "", db, reg, sm, clock);
  EXPECT_EQ(r.status, 400);
  EXPECT_NE(r.body.find("invalid_scope"), std::string::npos);
}

TEST(IdpAuthorize, RequiresState_CsrfDefense) {
  MockMongodbClient db;
  auto reg = registry_with_xpmile_spa(db);
  TestClock clock;
  idp::IdpSessionManager sm(db, clock);
  auto q = valid_query();
  q.erase("state");
  auto r = idp::authorize(q, "", db, reg, sm, clock);
  EXPECT_EQ(r.status, 400);
}

// ── no session → redirect to /login + persist pending_auth ──────────────────

TEST(IdpAuthorize, NoSession_PersistsPendingAuth_AndRedirectsToLogin) {
  MockMongodbClient db;
  auto reg = registry_with_xpmile_spa(db);
  TestClock clock;
  clock.now = 1000;
  idp::IdpSessionManager sm(db, clock);

  auto r = idp::authorize(valid_query(), /*cookie_header=*/"", db, reg, sm, clock);

  EXPECT_EQ(r.status, 302);
  EXPECT_EQ(r.location, "/idp/login");
  EXPECT_NE(r.set_cookie.find("xpmile_idp_pending="), std::string::npos);

  // Verify the pending_auth doc was persisted.
  ASSERT_EQ(db.creates.size(), 1u);
  EXPECT_EQ(db.creates[0].db,   "idp");
  EXPECT_EQ(db.creates[0].coll, "idp_pending_auth");
  auto doc = nlohmann::json::parse(db.creates[0].doc);
  EXPECT_EQ(doc["client_id"],    "xpmile-spa");
  EXPECT_EQ(doc["redirect_uri"], "https://marvel.app/cb");
  EXPECT_EQ(doc["state"],        "RP-STATE-1");
  EXPECT_EQ(doc["nonce"],        "RP-NONCE-1");
  EXPECT_EQ(doc["expiresAt"].get<std::int64_t>(), 1000 + 600);
}

// ── valid session → mint code + redirect to RP ──────────────────────────────

TEST(IdpAuthorize, ValidSession_MintsCode_AndRedirectsToRp) {
  MockMongodbClient db;
  auto reg = registry_with_xpmile_spa(db);
  TestClock clock;
  clock.now = 1000;
  idp::IdpSessionManager sm(db, clock);

  // Seed a valid session for "SID1".
  nlohmann::json session = {
      {"_id",         "SID1"},
      {"accountCode", "alice"},
      {"email",       "alice@x.com"},
      {"name",        "Alice"},
      {"role",        "Customer"},
      {"expiresAt",   99999},
  };
  db.canned_get_document["sessions"] = session.dump();

  auto r = idp::authorize(valid_query(),
                            "xpmile_idp_session=SID1",
                            db, reg, sm, clock);

  EXPECT_EQ(r.status, 302);
  // Location should start with the registered redirect_uri and contain
  // code= and state=.
  EXPECT_TRUE(r.location.rfind("https://marvel.app/cb?", 0) == 0)
      << "actual: " << r.location;
  EXPECT_NE(r.location.find("code=") , std::string::npos);
  EXPECT_NE(r.location.find("&state=RP-STATE-1"), std::string::npos);

  // Verify the code doc was persisted.
  ASSERT_EQ(db.creates.size(), 1u);
  EXPECT_EQ(db.creates[0].db,   "idp");
  EXPECT_EQ(db.creates[0].coll, "idp_codes");
  auto doc = nlohmann::json::parse(db.creates[0].doc);
  EXPECT_EQ(doc["client_id"],            "xpmile-spa");
  EXPECT_EQ(doc["user_sub"],             "alice");
  EXPECT_EQ(doc["user_email"],           "alice@x.com");
  EXPECT_EQ(doc["redirect_uri"],         "https://marvel.app/cb");
  EXPECT_EQ(doc["nonce"],                "RP-NONCE-1");
  EXPECT_EQ(doc["code_challenge"],       "PKCE-CHALLENGE-1");
  EXPECT_EQ(doc["code_challenge_method"], "S256");
  EXPECT_EQ(doc["expiresAt"].get<std::int64_t>(), 1000 + 30);
}

TEST(IdpAuthorize, ExpiredSession_TreatedAsNoSession) {
  MockMongodbClient db;
  auto reg = registry_with_xpmile_spa(db);
  TestClock clock;
  clock.now = 1000000;  // very far in the future
  idp::IdpSessionManager sm(db, clock);

  nlohmann::json session = {
      {"_id", "SID1"}, {"accountCode", "alice"}, {"expiresAt", 500},
  };
  db.canned_get_document["sessions"] = session.dump();

  auto r = idp::authorize(valid_query(),
                            "xpmile_idp_session=SID1",
                            db, reg, sm, clock);

  // Expired → falls through to the "no session" path.
  EXPECT_EQ(r.status, 302);
  EXPECT_EQ(r.location, "/idp/login");
  ASSERT_EQ(db.creates.size(), 1u);
  EXPECT_EQ(db.creates[0].coll, "idp_pending_auth");
}
