#ifndef IDP_PASSWORD_HASHER_HPP
#define IDP_PASSWORD_HASHER_HPP

#include <string>

/**
 * @file idp_password_hasher.hpp
 * @brief Abstract password hasher — what password reset (/reset_confirm)
 *        uses to produce the hash stored in idp.account.passwordHash.
 *
 * Separate from IPasswordVerifier (which only ever needs to /check/ a
 * password) so production code can implement one without dragging in
 * the other. The production impl will wrap whatever scheme the project
 * already uses (extracted in Phase K alongside the legacy login wiring);
 * tests use a deterministic mock that returns "hash:" + plaintext.
 */
namespace idp {

class IPasswordHasher {
public:
  virtual ~IPasswordHasher() = default;
  /// Produce the hash to store for @p password.
  virtual std::string hash(const std::string &password) = 0;
};

} // namespace idp

#endif // IDP_PASSWORD_HASHER_HPP
