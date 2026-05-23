#include "idp_login.hpp"

#include <iomanip>
#include <sstream>

#include "idp_cookie.hpp"
#include "idp_password.hpp"
#include "idp_session.hpp"
#include "json.hpp"
#include "mongodbc.hpp"

namespace idp {

namespace {

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

std::string url_encode(const std::string &s) {
  std::ostringstream os;
  os << std::hex << std::uppercase;
  for (unsigned char c : s) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' ||
        c == '.' || c == '~') {
      os << static_cast<char>(c);
    } else {
      os << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(c);
    }
  }
  return os.str();
}

} // namespace

sso::SsoHttpResult login(
    const std::string &user,
    const std::string &password,
    const std::string &cookie_header,
    IMongodbClient &db,
    IdpSessionManager &sessions,
    IPasswordVerifier &verifier,
    sso::IClock &clock) {

  if (user.empty() || password.empty()) {
    return error_response(400, "invalid_request",
                            "username and password are required");
  }

  // Resolve the pending-auth cookie.
  const std::string req_token =
      parse_cookie(cookie_header, kIdpPendingCookieName);
  if (req_token.empty()) {
    return error_response(400, "invalid_request",
                            "missing or expired pending-auth cookie");
  }

  // Look up the pending_auth doc.
  const nlohmann::json pending_query = {{"_id", req_token}};
  const std::string pending_raw =
      db.get_document("idp_pending_auth", pending_query.dump(), "{}");
  if (pending_raw.empty()) {
    return error_response(400, "invalid_request",
                            "pending-auth not found");
  }

  nlohmann::json pending;
  try {
    pending = nlohmann::json::parse(pending_raw);
  } catch (const std::exception &) {
    return error_response(400, "invalid_request",
                            "malformed pending-auth");
  }

  const std::int64_t now = clock.now_unix();
  if (pending.value("expiresAt", std::int64_t{0}) <= now) {
    return error_response(400, "invalid_request", "pending-auth expired");
  }

  // Look up the account in idp.account.
  const nlohmann::json acct_query = {{"accountCode", user}};
  const std::string acct_raw =
      db.get_document("account", acct_query.dump(), "{}");

  // Same error response whether the account is missing or the password
  // is wrong — no account enumeration via /login. (The IdP /authorize
  // already exposes user_sub via the issued code, but only AFTER the
  // password check passes.)
  auto wrong_credentials = []() {
    return error_response(401, "invalid_credentials", "wrong credentials");
  };

  if (acct_raw.empty()) {
    return wrong_credentials();
  }
  nlohmann::json acct;
  try {
    acct = nlohmann::json::parse(acct_raw);
  } catch (const std::exception &) {
    return error_response(500, "server_error", "malformed account doc");
  }

  const std::string hash = acct.value("passwordHash", std::string{});
  if (!verifier.verify(password, hash)) {
    return wrong_credentials();
  }

  // Mint the IdP session.
  NewIdpSessionParams params;
  params.accountCode = user;
  params.email       = acct.value("email", std::string{});
  params.name        = acct.value("name",  std::string{});
  params.role        = acct.value("role",  std::string{});
  const std::string sid = sessions.create(params);
  if (sid.empty()) {
    return error_response(500, "server_error", "could not mint session");
  }

  // Redirect back to /authorize with the original parameters.
  std::ostringstream loc;
  loc << "/api/v1/idp/authorize"
      << "?response_type=code"
      << "&client_id="     << url_encode(pending.value("client_id",    ""))
      << "&redirect_uri="  << url_encode(pending.value("redirect_uri", ""))
      << "&scope="         << url_encode(pending.value("scope",        ""))
      << "&state="         << url_encode(pending.value("state",        ""))
      << "&nonce="         << url_encode(pending.value("nonce",        ""))
      << "&code_challenge="<< url_encode(pending.value("code_challenge", ""))
      << "&code_challenge_method="
                           << url_encode(pending.value("code_challenge_method",
                                                      std::string{"S256"}));

  sso::SsoHttpResult r;
  r.status     = 302;
  r.location   = loc.str();
  r.set_cookie = build_idp_session_cookie(sid);
  return r;
}

} // namespace idp
