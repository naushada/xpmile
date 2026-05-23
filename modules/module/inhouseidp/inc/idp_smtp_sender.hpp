#ifndef IDP_SMTP_SENDER_HPP
#define IDP_SMTP_SENDER_HPP

#include <string>

#include "idp_email_sender.hpp"

/**
 * @file idp_smtp_sender.hpp
 * @brief Production `IEmailSender` impl — thin wrapper around the
 *        existing `SMTP::Account` + `SMTP::User` (emailservice.hpp)
 *        pipeline that the project already uses for outbound mail.
 *
 * Reads SMTP credentials from the env on every send so the IdP dyno
 * doesn't carry secrets in code. The expected env vars are documented
 * on the constructor.
 *
 * `SMTP::Account` is a singleton with mutable per-call state, so this
 * impl serialises its sends behind an internal mutex — fine for
 * password-reset traffic (low rate, never on the hot path).
 */
namespace idp {

class SmtpEmailSender : public IEmailSender {
public:
  /**
   * @brief Construct an SMTP sender.
   *
   * Required env vars on the dyno (read inside @c send, not at
   * construction — lets a stale config-var fix take effect without
   * a redeploy):
   *
   *   - `SMTP_FROM_EMAIL`     — sender address (e.g. no-reply@xpmile.app)
   *   - `SMTP_FROM_PASSWORD`  — SMTP auth password for the above
   *
   * Optional:
   *
   *   - `SMTP_FROM_NAME`      — display name (default: empty, address only)
   *
   * `send()` returns false (and logs) if either required var is unset
   * or empty — the caller (password_reset::reset_request) intentionally
   * doesn't gate its 200 on this, so a transient SMTP failure can't
   * tell an attacker which accounts exist.
   */
  SmtpEmailSender() = default;

  bool send(const std::string &to,
             const std::string &subject,
             const std::string &body) override;
};

} // namespace idp

#endif // IDP_SMTP_SENDER_HPP
