// modules/module/inhouseidp/test/login_test.cc
//
// Tests for the /login endpoint logic (Phase D part 2).

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "idp_cookie.hpp"
#include "idp_login.hpp"
#include "idp_password.hpp"
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

class MockPasswordVerifier : public idp::IPasswordVerifier {
public:
  bool next_result = true;
  std::string last_password;
  std::string last_hash;
  bool verify(const std::string &password, const std::string &hash) override {
    last_password = password;
    last_hash     = hash;
    return next_result;
  }
};

class MockMongodbClient : public IMongodbClient {
public:
  struct CreateCall { std::string db, coll, doc; };
  std::vector<CreateCall> creates;

  std::map<std::string, std::string> canned_get_document;
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

  // ── stubs ───────────────────────────────────────────────────────────────
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

void seed_valid_pending(MockMongodbClient &db, std::int64_t now = 1000) {
  nlohmann::json pending = {
      {"_id",                    "RT1"},
      {"client_id",              "xpmile-spa"},
      {"redirect_uri",           "https://marvel.app/cb"},
      {"scope",                  "openid email"},
      {"state",                  "RP-STATE"},
      {"nonce",                  "RP-NONCE"},
      {"code_challenge",         "PKCE-1"},
      {"code_challenge_method",  "S256"},
      {"expiresAt",              now + 600},
  };
  db.canned_get_document["idp_pending_auth"] = pending.dump();
}

void seed_valid_account(MockMongodbClient &db) {
  nlohmann::json acct = {
      {"accountCode",  "alice"},
      {"passwordHash", "$HASH$"},
      {"email",        "alice@x.com"},
      {"name",         "Alice"},
      {"role",         "Customer"},
  };
  db.canned_get_document["account"] = acct.dump();
}

} // namespace

TEST(IdpLogin, ValidCredentials_CreatesSession_RedirectsToAuthorize) {
  MockMongodbClient db;
  MockPasswordVerifier verifier;
  TestClock clock;
  clock.now = 1000;
  seed_valid_pending(db);
  seed_valid_account(db);

  idp::IdpSessionManager sm(db, clock);
  auto r = idp::login("alice", "pw",
                       "xpmile_idp_pending=RT1",
                       db, sm, verifier, clock);

  EXPECT_EQ(r.status, 302);
  EXPECT_TRUE(r.location.rfind("/api/v1/idp/authorize?", 0) == 0)
      << "actual: " << r.location;
  // Original params propagated:
  EXPECT_NE(r.location.find("client_id=xpmile-spa"),            std::string::npos);
  EXPECT_NE(r.location.find("redirect_uri="),                   std::string::npos);
  EXPECT_NE(r.location.find("state=RP-STATE"),                  std::string::npos);
  EXPECT_NE(r.location.find("code_challenge_method=S256"),      std::string::npos);
  // Session cookie was set:
  EXPECT_NE(r.set_cookie.find("xpmile_idp_session="),           std::string::npos);

  // Session was persisted.
  ASSERT_EQ(db.creates.size(), 1u);
  EXPECT_EQ(db.creates[0].db,   "idp");
  EXPECT_EQ(db.creates[0].coll, "sessions");
  auto sess = nlohmann::json::parse(db.creates[0].doc);
  EXPECT_EQ(sess["accountCode"], "alice");
  EXPECT_EQ(sess["email"],       "alice@x.com");
  EXPECT_EQ(sess["role"],        "Customer");

  // verifier was called with the right inputs.
  EXPECT_EQ(verifier.last_password, "pw");
  EXPECT_EQ(verifier.last_hash,     "$HASH$");
}

TEST(IdpLogin, EmptyCredentials_Returns_400) {
  MockMongodbClient db;
  MockPasswordVerifier verifier;
  TestClock clock;
  idp::IdpSessionManager sm(db, clock);

  auto r = idp::login("", "", "xpmile_idp_pending=RT1",
                       db, sm, verifier, clock);
  EXPECT_EQ(r.status, 400);
}

TEST(IdpLogin, MissingPendingAuthCookie_Returns_400) {
  MockMongodbClient db;
  MockPasswordVerifier verifier;
  TestClock clock;
  idp::IdpSessionManager sm(db, clock);

  auto r = idp::login("alice", "pw", /*cookie_header=*/"",
                       db, sm, verifier, clock);
  EXPECT_EQ(r.status, 400);
}

TEST(IdpLogin, UnknownPendingAuth_Returns_400) {
  MockMongodbClient db;
  MockPasswordVerifier verifier;
  TestClock clock;
  // No seed → get_document returns empty.
  idp::IdpSessionManager sm(db, clock);

  auto r = idp::login("alice", "pw", "xpmile_idp_pending=RT1",
                       db, sm, verifier, clock);
  EXPECT_EQ(r.status, 400);
}

TEST(IdpLogin, ExpiredPendingAuth_Returns_400) {
  MockMongodbClient db;
  MockPasswordVerifier verifier;
  TestClock clock;
  clock.now = 99999;
  seed_valid_pending(db, /*now=*/1000);  // expiresAt = 1600 — long past
  seed_valid_account(db);

  idp::IdpSessionManager sm(db, clock);
  auto r = idp::login("alice", "pw", "xpmile_idp_pending=RT1",
                       db, sm, verifier, clock);
  EXPECT_EQ(r.status, 400);
}

TEST(IdpLogin, WrongPassword_Returns_401_NoSessionCreated) {
  MockMongodbClient db;
  MockPasswordVerifier verifier;
  verifier.next_result = false;  // hash mismatch
  TestClock clock;
  seed_valid_pending(db);
  seed_valid_account(db);

  idp::IdpSessionManager sm(db, clock);
  auto r = idp::login("alice", "wrong",
                       "xpmile_idp_pending=RT1",
                       db, sm, verifier, clock);
  EXPECT_EQ(r.status, 401);
  EXPECT_NE(r.body.find("invalid_credentials"), std::string::npos);
  EXPECT_TRUE(db.creates.empty()) << "no session should have been created";
}

TEST(IdpLogin, UnknownAccount_Returns_401_SameAs_WrongPassword) {
  MockMongodbClient db;
  MockPasswordVerifier verifier;
  TestClock clock;
  seed_valid_pending(db);
  // No account seeded → get_document("account",...) returns empty.

  idp::IdpSessionManager sm(db, clock);
  auto r = idp::login("nonexistent", "pw",
                       "xpmile_idp_pending=RT1",
                       db, sm, verifier, clock);

  EXPECT_EQ(r.status, 401);
  // Same body shape as wrong-password — no enumeration leak.
  EXPECT_NE(r.body.find("invalid_credentials"), std::string::npos);
}
