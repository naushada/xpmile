#ifndef IDP_PASSWORD_RESET_HPP
#define IDP_PASSWORD_RESET_HPP

#include <cstddef>
#include <string>

#include "sso_endpoints.hpp"  // sso::SsoHttpResult
#include "sso_session.hpp"     // sso::IClock

class IMongodbClient;

/**
 * @file idp_password_reset.hpp
 * @brief Transport-agnostic logic for the password-reset endpoints
 *        (docs/design/inhouse-idp/inhouse-idp-design.md §3, reset flow
 *         ASCII diagram).
 *
 * Two endpoints, on the IdP host:
 *
 *   POST /api/v1/idp/password/reset_request
 *     Body: {"email":"..."} or {"accountCode":"..."}
 *     Look up the account; if found, mint a one-time token in
 *     idp.password_reset_tokens with TTL = cfg.token_ttl_sec, and
 *     email the user a reset link {portal_base_url}/password/reset
 *     ?token=...
 *     ALWAYS returns 200 — no enumeration of which emails exist.
 *
 *   POST /api/v1/idp/password/reset_confirm
 *     Body: {"token":"...", "new_password":"..."}
 *     Validate token TTL, hash the new password, update
 *     idp.account.passwordHash, delete the token (one-time use),
 *     revoke all live IdP sessions for the account (force re-login).
 *     Returns 200 on success, 400 on any failure.
 */
namespace idp {

class IEmailSender;
class IPasswordHasher;
class IdpSessionManager;

struct PasswordResetConfig {
  std::string portal_base_url;            ///< e.g. https://idp.example
  std::string from_address;               ///< From: header on the reset email
  long        token_ttl_sec        = 3600;   ///< 1 hour default
  std::size_t min_password_length  = 8;    ///< complexity floor
};

sso::SsoHttpResult reset_request(
    const std::string &email,
    const std::string &account_code,   // either; email wins when both present
    IMongodbClient &db,
    IEmailSender &email_sender,
    sso::IClock &clock,
    const PasswordResetConfig &cfg);

sso::SsoHttpResult reset_confirm(
    const std::string &token,
    const std::string &new_password,
    IMongodbClient &db,
    IPasswordHasher &hasher,
    IdpSessionManager &sessions,
    sso::IClock &clock,
    const PasswordResetConfig &cfg);

} // namespace idp

#endif // IDP_PASSWORD_RESET_HPP
