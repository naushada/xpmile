#ifndef IDP_PASSWORD_HPP
#define IDP_PASSWORD_HPP

#include <string>

/**
 * @file idp_password.hpp
 * @brief Password-verification interface used by /login and the legacy
 *        login fallback (Phase K).
 *
 * Production implementation wraps the existing project's password-hashing
 * scheme (extracted from handle_account_login_POST in Phase K). Tests use
 * a mock that returns deterministic results — they're not concerned with
 * the hash format, only with /login's branching on the bool result.
 */
namespace idp {

class IPasswordVerifier {
public:
  virtual ~IPasswordVerifier() = default;
  /// @return true iff @p password matches @p hash.
  virtual bool verify(const std::string &password,
                       const std::string &hash) = 0;
};

} // namespace idp

#endif // IDP_PASSWORD_HPP
