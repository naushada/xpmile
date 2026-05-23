#ifndef SSO_JWT_HPP
#define SSO_JWT_HPP

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <openssl/evp.h>

/**
 * @file sso_jwt.hpp
 * @brief RS256 id_token verification against a JWKS
 *        (docs/design/sso/sso-design.md §4).
 *
 * RS256 is the @e only accepted algorithm. Any other `alg` — including
 * `"none"` and every HMAC variant — is rejected before any signature work, so
 * the JWT algorithm-confusion attack has no code path to exploit.
 */
namespace sso {

namespace detail {
struct EvpPkeyDeleter {
  void operator()(EVP_PKEY *p) const noexcept { EVP_PKEY_free(p); }
};
} // namespace detail

using EvpPkeyPtr = std::unique_ptr<EVP_PKEY, detail::EvpPkeyDeleter>;

/// A set of RSA public keys parsed from a JWKS document, indexed by `kid`.
class Jwks {
public:
  /**
   * @brief Parse a JWKS JSON document, replacing any previously-loaded keys.
   * @return false if the JSON is malformed; true otherwise (a valid document
   *         with no RSA keys yields true and an empty set).
   */
  bool parse(const std::string &jwks_json);

  std::size_t size() const { return m_keys.size(); }
  bool        has_key(const std::string &kid) const;

  /// Look up a key by `kid`. Returns nullptr when absent.
  EVP_PKEY *get(const std::string &kid) const;

private:
  std::map<std::string, EvpPkeyPtr> m_keys;
};

/// What an id_token is checked against.
struct JwtExpect {
  std::string issuer;     ///< expected `iss`
  std::string client_id;  ///< expected `aud` (and `azp` when multi-audience)
  std::string nonce;      ///< expected `nonce`
};

/// Outcome of verifying an id_token. On failure, @ref error explains why.
struct JwtResult {
  bool        ok = false;
  std::string error;
  std::string subject;        ///< `sub`
  std::string email;
  bool        email_verified = false;
  std::string name;
  std::vector<std::string> groups;
};

/**
 * @brief Verify an RS256 id_token against a JWKS.
 * @param token      The compact-serialized JWT.
 * @param jwks       Keys to resolve the token's `kid` against.
 * @param expect     Expected issuer / audience / nonce.
 * @param clock_now  Current time, Unix epoch seconds.
 */
JwtResult verify_jwt(const std::string &token, const Jwks &jwks,
                     const JwtExpect &expect, std::int64_t clock_now);

} // namespace sso

#endif // SSO_JWT_HPP
