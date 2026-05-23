// modules/module/inhouseidp/test/password_reset_test.cc
//
// Tests for the password-reset endpoints (Phase G). The two endpoints
// share a mock-rich setup: a mongo mock that captures creates/updates/
// deletes and serves canned per-collection get_document responses, plus
// a mock IPasswordHasher and IEmailSender.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "idp_email_sender.hpp"
#include "idp_password_hasher.hpp"
#include "idp_password_reset.hpp"
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

class MockEmailSender : public idp::IEmailSender {
public:
  struct Sent { std::string to, subject, body; };
  std::vector<Sent> sent;
  bool next_result = true;
  bool send(const std::string &to, const std::string &subject,
             const std::string &body) override {
    sent.push_back({to, subject, body});
    return next_result;
  }
};

class MockHasher : public idp::IPasswordHasher {
public:
  std::string last_password;
  std::string hash(const std::string &password) override {
    last_password = password;
    return "hash:" + password;
  }
};

class MockMongodbClient : public IMongodbClient {
public:
  struct CreateCall { std::string db, coll, doc; };
  struct UpdateCall { std::string coll, filter, update; };
  struct DeleteCall { std::string coll, doc; };
  std::vector<CreateCall> creates;
  std::vector<UpdateCall> updates;
  std::vector<DeleteCall> deletes;

  std::map<std::string, std::string> canned_get_document;  // by collection
  bool update_returns = true;
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
  bool update_collection(const std::string &coll, const std::string &filter,
                          const std::string &update) override {
    updates.push_back({coll, filter, update});
    return update_returns;
  }
  bool delete_document(const std::string &coll, const std::string &doc) override {
    deletes.push_back({coll, doc});
    return true;
  }

  // ── stubs ───────────────────────────────────────────────────────────────
  std::int32_t create_bulk_document(const std::string &, const std::string &,
                                    const std::string &) override { return 0; }
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

idp::PasswordResetConfig test_cfg() {
  idp::PasswordResetConfig cfg;
  cfg.portal_base_url      = "https://idp.example";
  cfg.from_address         = "no-reply@xpmile.app";
  cfg.token_ttl_sec        = 3600;
  cfg.min_password_length  = 8;
  return cfg;
}

void seed_account(MockMongodbClient &db, const std::string &code = "alice",
                   const std::string &email = "alice@x.com") {
  nlohmann::json acct = {
      {"accountCode", code},
      {"email",       email},
      {"name",        "Alice"},
  };
  db.canned_get_document["account"] = acct.dump();
}

void seed_reset_token(MockMongodbClient &db, const std::string &token,
                        const std::string &acct_code,
                        std::int64_t expires_at) {
  nlohmann::json doc = {
      {"_id",         token},
      {"accountCode", acct_code},
      {"email",       "alice@x.com"},
      {"createdAt",   500},
      {"expiresAt",   expires_at},
  };
  db.canned_get_document["password_reset_tokens"] = doc.dump();
}

} // namespace

// ── reset_request ────────────────────────────────────────────────────────────

TEST(PasswordResetRequest, ValidEmail_PersistsToken_AndSendsEmail) {
  MockMongodbClient db;
  MockEmailSender email;
  TestClock clock; clock.now = 1000;
  seed_account(db);

  auto r = idp::reset_request("alice@x.com", "", db, email, clock, test_cfg());

  EXPECT_EQ(r.status, 200);

  // Token was persisted to the right collection in the right db.
  ASSERT_EQ(db.creates.size(), 1u);
  EXPECT_EQ(db.creates[0].db,   "idp");
  EXPECT_EQ(db.creates[0].coll, "password_reset_tokens");
  auto tok_doc = nlohmann::json::parse(db.creates[0].doc);
  EXPECT_EQ(tok_doc["accountCode"], "alice");
  EXPECT_EQ(tok_doc["email"],       "alice@x.com");
  EXPECT_EQ(tok_doc["expiresAt"].get<std::int64_t>(), 1000 + 3600);

  // Email was sent — to the account's address, containing the
  // token in a reset URL under portal_base_url.
  ASSERT_EQ(email.sent.size(), 1u);
  EXPECT_EQ(email.sent[0].to, "alice@x.com");
  EXPECT_NE(email.sent[0].body.find("https://idp.example/password/reset?token="),
            std::string::npos);
  EXPECT_NE(email.sent[0].body.find(tok_doc["_id"].get<std::string>()),
            std::string::npos);
}

TEST(PasswordResetRequest, UnknownEmail_Returns_200_NoTokenNoEmail) {
  // Enumeration defence: the response shape must NOT differ between
  // "real" and "unknown" addresses.
  MockMongodbClient db;
  MockEmailSender email;
  TestClock clock;
  // No seed → get_document returns empty.

  auto r = idp::reset_request("ghost@x.com", "", db, email, clock, test_cfg());

  EXPECT_EQ(r.status, 200);
  EXPECT_TRUE(db.creates.empty()) << "no token should be persisted";
  EXPECT_TRUE(email.sent.empty())  << "no email should be sent";
}

TEST(PasswordResetRequest, AccountCodeLookup_AlsoWorks) {
  MockMongodbClient db;
  MockEmailSender email;
  TestClock clock;
  seed_account(db);
  auto r = idp::reset_request("", "alice", db, email, clock, test_cfg());
  EXPECT_EQ(r.status, 200);
  EXPECT_EQ(db.creates.size(), 1u);
  EXPECT_EQ(email.sent.size(), 1u);
}

TEST(PasswordResetRequest, MissingBothFields_Returns_400) {
  MockMongodbClient db;
  MockEmailSender email;
  TestClock clock;
  auto r = idp::reset_request("", "", db, email, clock, test_cfg());
  EXPECT_EQ(r.status, 400);
}

// ── reset_confirm ────────────────────────────────────────────────────────────

TEST(PasswordResetConfirm, ValidToken_UpdatesPassword_DeletesToken) {
  MockMongodbClient db;
  MockHasher hasher;
  TestClock clock; clock.now = 1000;
  idp::IdpSessionManager sm(db, clock);

  seed_reset_token(db, "RT1", "alice", /*expires_at=*/99999);

  auto r = idp::reset_confirm("RT1", "newpassword123",
                                db, hasher, sm, clock, test_cfg());

  EXPECT_EQ(r.status, 200);
  EXPECT_EQ(hasher.last_password, "newpassword123");

  // The account update happened — filter by accountCode, $set passwordHash.
  ASSERT_GE(db.updates.size(), 1u);
  bool saw_account_update = false;
  for (const auto &u : db.updates) {
    if (u.coll == "account" &&
        u.update.find("\"passwordHash\":\"hash:newpassword123\"") != std::string::npos) {
      saw_account_update = true;
      EXPECT_NE(u.filter.find("\"accountCode\":\"alice\""), std::string::npos);
    }
  }
  EXPECT_TRUE(saw_account_update);

  // The token was burned.
  bool saw_token_delete = false;
  for (const auto &d : db.deletes) {
    if (d.coll == "password_reset_tokens" &&
        d.doc.find("\"_id\":\"RT1\"") != std::string::npos) {
      saw_token_delete = true;
    }
  }
  EXPECT_TRUE(saw_token_delete);
}

TEST(PasswordResetConfirm, UnknownToken_Returns_400) {
  MockMongodbClient db;
  MockHasher hasher;
  TestClock clock;
  idp::IdpSessionManager sm(db, clock);
  // No seed → token lookup returns empty.
  auto r = idp::reset_confirm("UNKNOWN", "newpassword123",
                                db, hasher, sm, clock, test_cfg());
  EXPECT_EQ(r.status, 400);
  EXPECT_TRUE(db.updates.empty());
}

TEST(PasswordResetConfirm, ExpiredToken_Returns_400) {
  MockMongodbClient db;
  MockHasher hasher;
  TestClock clock; clock.now = 99999;
  idp::IdpSessionManager sm(db, clock);
  seed_reset_token(db, "RT1", "alice", /*expires_at=*/500);
  auto r = idp::reset_confirm("RT1", "newpassword123",
                                db, hasher, sm, clock, test_cfg());
  EXPECT_EQ(r.status, 400);
  EXPECT_NE(r.body.find("expired"), std::string::npos);
}

TEST(PasswordResetConfirm, MissingToken_Returns_400) {
  MockMongodbClient db;
  MockHasher hasher;
  TestClock clock;
  idp::IdpSessionManager sm(db, clock);
  auto r = idp::reset_confirm("", "newpassword123",
                                db, hasher, sm, clock, test_cfg());
  EXPECT_EQ(r.status, 400);
}

TEST(PasswordResetConfirm, PasswordTooShort_Returns_400) {
  MockMongodbClient db;
  MockHasher hasher;
  TestClock clock;
  idp::IdpSessionManager sm(db, clock);
  seed_reset_token(db, "RT1", "alice", /*expires_at=*/99999);
  auto r = idp::reset_confirm("RT1", "short",
                                db, hasher, sm, clock, test_cfg());
  EXPECT_EQ(r.status, 400);
  EXPECT_NE(r.body.find("weak_password"), std::string::npos);
  // Hasher must NOT have been called.
  EXPECT_TRUE(hasher.last_password.empty());
  // No DB writes.
  EXPECT_TRUE(db.updates.empty());
}

TEST(PasswordResetConfirm, AccountGone_BetweenTokenAndUpdate_Returns_400) {
  MockMongodbClient db;
  MockHasher hasher;
  TestClock clock;
  idp::IdpSessionManager sm(db, clock);
  seed_reset_token(db, "RT1", "alice", /*expires_at=*/99999);
  db.update_returns = false;  // account update finds 0 docs
  auto r = idp::reset_confirm("RT1", "newpassword123",
                                db, hasher, sm, clock, test_cfg());
  EXPECT_EQ(r.status, 400);
  // Token should NOT have been deleted on a failed update.
  EXPECT_TRUE(db.deletes.empty());
}

TEST(PasswordResetConfirm, SuccessfulReset_RevokesAllSessions) {
  // Verify that reset_confirm triggers session revocation by checking
  // for the underlying delete on `sessions` with an accountCode filter.
  // (IdpSessionManager::revoke_all_for_account uses delete_document with
  //  {accountCode: ...} so the on-prem mongo deletes every matching row.)
  MockMongodbClient db;
  MockHasher hasher;
  TestClock clock;
  idp::IdpSessionManager sm(db, clock);
  seed_reset_token(db, "RT1", "alice", /*expires_at=*/99999);

  auto r = idp::reset_confirm("RT1", "newpassword123",
                                db, hasher, sm, clock, test_cfg());
  ASSERT_EQ(r.status, 200);

  bool saw_session_revoke = false;
  for (const auto &d : db.deletes) {
    if (d.coll == "sessions" &&
        d.doc.find("\"accountCode\":\"alice\"") != std::string::npos) {
      saw_session_revoke = true;
    }
  }
  EXPECT_TRUE(saw_session_revoke)
      << "expected a delete_document call on `sessions` filtered by accountCode";
}
