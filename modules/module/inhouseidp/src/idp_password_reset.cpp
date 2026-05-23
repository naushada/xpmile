#include "idp_password_reset.hpp"

#include <cstdint>
#include <sstream>

#include "idp_email_sender.hpp"
#include "idp_password_hasher.hpp"
#include "idp_session.hpp"
#include "json.hpp"
#include "mongodbc.hpp"
#include "sso_util.hpp"  // sso::random_token

namespace idp {

namespace {

inline constexpr const char *kResetTokenColl = "password_reset_tokens";
inline constexpr const char *kAccountColl    = "account";

sso::SsoHttpResult ok_empty() {
  sso::SsoHttpResult r;
  r.status       = 200;
  r.content_type = "application/json";
  r.body         = "{}";
  return r;
}

sso::SsoHttpResult error_response(int status, const std::string &error,
                                    const std::string &description = {}) {
  sso::SsoHttpResult r;
  r.status       = status;
  r.content_type = "application/json";
  nlohmann::json body = {{"error", error}};
  if (!description.empty()) body["error_description"] = description;
  r.body = body.dump();
  return r;
}

std::string look_up_account_raw(IMongodbClient &db,
                                  const std::string &email,
                                  const std::string &account_code) {
  // Email wins when both are present (matches what the design says).
  if (!email.empty()) {
    const nlohmann::json q = {{"email", email}};
    return db.get_document(kAccountColl, q.dump(), "{}");
  }
  if (!account_code.empty()) {
    const nlohmann::json q = {{"accountCode", account_code}};
    return db.get_document(kAccountColl, q.dump(), "{}");
  }
  return {};
}

std::string build_reset_email_body(const std::string &portal_base,
                                     const std::string &token,
                                     long ttl_sec) {
  std::ostringstream os;
  os << "Hello,\n\n"
     << "We received a request to reset your xpmile password. To "
        "continue, open the link below:\n\n"
     << portal_base << "/password/reset?token=" << token << "\n\n"
     << "This link expires in " << (ttl_sec / 60) << " minutes. If you "
        "did not request this, you can safely ignore this email — your "
        "current password remains unchanged.\n\n"
     << "— xpmile";
  return os.str();
}

} // namespace

sso::SsoHttpResult reset_request(
    const std::string &email,
    const std::string &account_code,
    IMongodbClient &db,
    IEmailSender &email_sender,
    sso::IClock &clock,
    const PasswordResetConfig &cfg) {

  if (email.empty() && account_code.empty()) {
    return error_response(400, "invalid_request",
                           "email or accountCode is required");
  }

  // Look up the account. We DO NOT report whether it exists or not —
  // both branches return the same 200 to defeat enumeration.
  const std::string acct_raw = look_up_account_raw(db, email, account_code);
  if (acct_raw.empty()) {
    return ok_empty();
  }

  nlohmann::json acct;
  try {
    acct = nlohmann::json::parse(acct_raw);
  } catch (const std::exception &) {
    // Don't surface internal trouble to the caller — same enumeration
    // logic applies. Return 200 silently.
    return ok_empty();
  }

  const std::string account_email =
      acct.value("email", std::string{});
  const std::string acct_code =
      acct.value("accountCode", std::string{});
  if (account_email.empty() || acct_code.empty()) {
    return ok_empty();
  }

  // Mint and persist the token.
  const std::int64_t now   = clock.now_unix();
  const std::string token  = sso::random_token(32);
  nlohmann::json token_doc = {
      {"_id",         token},
      {"accountCode", acct_code},
      {"email",       account_email},
      {"createdAt",   now},
      {"expiresAt",   now + cfg.token_ttl_sec},
  };
  db.create_document("idp", kResetTokenColl, token_doc.dump());

  // Send the email. (We don't gate the 200 on accept — the email
  // pipeline may be async; gating would leak information about
  // delivery success.)
  email_sender.send(account_email,
                     "Reset your xpmile password",
                     build_reset_email_body(cfg.portal_base_url,
                                              token,
                                              cfg.token_ttl_sec));
  return ok_empty();
}

sso::SsoHttpResult reset_confirm(
    const std::string &token,
    const std::string &new_password,
    IMongodbClient &db,
    IPasswordHasher &hasher,
    IdpSessionManager &sessions,
    sso::IClock &clock,
    const PasswordResetConfig &cfg) {

  if (token.empty()) {
    return error_response(400, "invalid_request", "missing token");
  }
  if (new_password.size() < cfg.min_password_length) {
    return error_response(400, "weak_password",
                           "password must be at least " +
                               std::to_string(cfg.min_password_length) +
                               " characters");
  }

  // Look up the reset token.
  const nlohmann::json q = {{"_id", token}};
  const std::string raw  = db.get_document(kResetTokenColl, q.dump(), "{}");
  if (raw.empty()) {
    return error_response(400, "invalid_token", "unknown or used token");
  }
  nlohmann::json tok_doc;
  try {
    tok_doc = nlohmann::json::parse(raw);
  } catch (const std::exception &) {
    return error_response(400, "invalid_token", "malformed token doc");
  }

  const std::int64_t now = clock.now_unix();
  if (tok_doc.value("expiresAt", std::int64_t{0}) <= now) {
    return error_response(400, "invalid_token", "token expired");
  }

  const std::string acct_code =
      tok_doc.value("accountCode", std::string{});
  if (acct_code.empty()) {
    return error_response(400, "invalid_token", "token missing accountCode");
  }

  // Update the account's password hash.
  const std::string new_hash = hasher.hash(new_password);
  const nlohmann::json acct_filter = {{"accountCode", acct_code}};
  const nlohmann::json acct_update = {
      {"$set", {{"passwordHash", new_hash},
                {"passwordUpdatedAt", now}}},
  };
  if (!db.update_collection(kAccountColl,
                              acct_filter.dump(),
                              acct_update.dump())) {
    return error_response(400, "invalid_token", "account no longer exists");
  }

  // Burn the token (one-time use). Best effort — even if delete returns
  // false (already gone) the update above is what protects.
  db.delete_document(kResetTokenColl, q.dump());

  // Revoke every live IdP session for the account so they must re-login
  // with the new password.
  sessions.revoke_all_for_account(acct_code);

  return ok_empty();
}

} // namespace idp
