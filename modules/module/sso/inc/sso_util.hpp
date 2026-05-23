#ifndef SSO_UTIL_HPP
#define SSO_UTIL_HPP

#include <cstddef>
#include <string>

/**
 * @file sso_util.hpp
 * @brief Small shared primitives for the SSO module — base64url + CSPRNG
 *        tokens. Used for session ids, PKCE verifiers, and OIDC state/nonce.
 */
namespace sso {

/// URL-safe base64 (RFC 4648 §5), no padding.
std::string base64url_encode(const unsigned char *data, std::size_t len);

/**
 * @brief Decode URL-safe base64 (RFC 4648 §5).
 *
 * Padding ('=') is tolerated but not required. Returns "" if @p in contains a
 * character outside the base64url alphabet.
 */
std::string base64url_decode(const std::string &in);

/// Standard base64 (RFC 4648 §4) with '=' padding — used for SAML messages.
std::string base64_encode(const std::string &in);

/**
 * @brief Decode standard base64 (RFC 4648 §4).
 *
 * Whitespace is skipped (IdP-emitted SAML base64 is often line-wrapped) and
 * padding is tolerated. Returns "" on a character outside the alphabet.
 */
std::string base64_decode(const std::string &in);

/**
 * @brief A URL-safe random token: @p nbytes of CSPRNG entropy, base64url-encoded.
 * @return The token, or "" if the system RNG fails or @p nbytes is 0.
 */
std::string random_token(std::size_t nbytes);

} // namespace sso

#endif // SSO_UTIL_HPP
