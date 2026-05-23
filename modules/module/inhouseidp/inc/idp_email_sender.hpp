#ifndef IDP_EMAIL_SENDER_HPP
#define IDP_EMAIL_SENDER_HPP

#include <string>

/**
 * @file idp_email_sender.hpp
 * @brief Abstract one-shot email sender for password-reset links.
 *
 * Intentionally minimal — we only need plaintext bodies, and we don't
 * want password-reset code to take a dependency on the full project
 * EmailService (with its SMTP FSM, retry queue, etc.). The production
 * impl wraps EmailService; tests use a mock that captures the message.
 */
namespace idp {

class IEmailSender {
public:
  virtual ~IEmailSender() = default;
  /// Send a single plaintext message. @return true iff accepted.
  virtual bool send(const std::string &to,
                     const std::string &subject,
                     const std::string &body) = 0;
};

} // namespace idp

#endif // IDP_EMAIL_SENDER_HPP
