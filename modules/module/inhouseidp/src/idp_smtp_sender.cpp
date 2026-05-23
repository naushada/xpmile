#include "idp_smtp_sender.hpp"

#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

#include <ace/Log_Msg.h>

#include "emailservice.hpp"

namespace idp {

namespace {

/// SMTP::Account is a process-wide singleton with mutable per-call
/// state — serialise IdP sends behind this mutex so two concurrent
/// reset_request handlers can't trample each other's recipient list
/// or body. Low-rate flow; not a hot path.
std::mutex &smtp_mutex() {
  static std::mutex m;
  return m;
}

std::string env_or_empty(const char *name) {
  const char *v = std::getenv(name);
  return (v && *v) ? std::string{v} : std::string{};
}

} // namespace

bool SmtpEmailSender::send(const std::string &to,
                            const std::string &subject,
                            const std::string &body) {
  const std::string from_email = env_or_empty("SMTP_FROM_EMAIL");
  const std::string from_pass  = env_or_empty("SMTP_FROM_PASSWORD");
  if (from_email.empty() || from_pass.empty()) {
    ACE_ERROR((LM_ERROR,
               ACE_TEXT("%D [SmtpEmailSender:%t] %M %N:%l SMTP_FROM_EMAIL or "
                        "SMTP_FROM_PASSWORD env var unset; cannot send to %s\n"),
               to.c_str()));
    return false;
  }
  const std::string from_name = env_or_empty("SMTP_FROM_NAME");

  if (to.empty()) {
    ACE_ERROR((LM_ERROR,
               ACE_TEXT("%D [SmtpEmailSender:%t] %M %N:%l empty recipient\n")));
    return false;
  }

  std::lock_guard<std::mutex> guard(smtp_mutex());

  SMTP::Account::instance()
      .from_email(from_email)
      .from_password(from_pass)
      .from_name(from_name)
      .to_email({to})
      .email_subject(subject)
      .email_body(body);

  // No attachments for the password-reset flow.
  SMTP::Account::instance().attachment({});

  SMTP::User mailer;
  std::int32_t rc = mailer.startEmailTransaction();
  if (rc < 0) {
    ACE_ERROR((LM_ERROR,
               ACE_TEXT("%D [SmtpEmailSender:%t] %M %N:%l SMTP transaction "
                        "to %s failed (rc=%d)\n"),
               to.c_str(), rc));
    return false;
  }
  ACE_DEBUG((LM_DEBUG,
             ACE_TEXT("%D [SmtpEmailSender:%t] %M %N:%l sent password-reset "
                      "email to %s (subject='%s')\n"),
             to.c_str(), subject.c_str()));
  return true;
}

} // namespace idp
