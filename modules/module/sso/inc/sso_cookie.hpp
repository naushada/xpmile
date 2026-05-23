#ifndef SSO_COOKIE_HPP
#define SSO_COOKIE_HPP

#include <string>

/**
 * @file sso_cookie.hpp
 * @brief Session-cookie build/parse helpers for the SSO feature.
 *
 * The session cookie is an opaque, server-looked-up id — never a JWT. See
 * docs/design/sso/sso-design.md §2.
 */
namespace sso {

/// Name of the session cookie.
inline constexpr const char *kSessionCookieName = "xpmile_session";

/// Default session/cookie lifetime in seconds (12 hours).
inline constexpr long kDefaultSessionMaxAge = 43200;

/**
 * @brief Build a Set-Cookie header value carrying the session id.
 *
 * Emits the standard security attributes: HttpOnly, Secure, SameSite=Lax,
 * Path=/. @p max_age is the cookie lifetime in seconds.
 */
std::string build_session_cookie(const std::string &sid,
                                  long max_age = kDefaultSessionMaxAge);

/**
 * @brief Build a Set-Cookie header value that expires the session cookie
 *        immediately (Max-Age=0) — used on logout.
 */
std::string build_expired_cookie();

/**
 * @brief Extract the xpmile_session value from a raw Cookie request header.
 *
 * @return the session id, or "" if absent or the header is malformed.
 */
std::string parse_session_cookie(const std::string &cookie_header);

} // namespace sso

#endif // SSO_COOKIE_HPP
