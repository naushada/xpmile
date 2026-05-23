#ifndef SSO_CSRF_HPP
#define SSO_CSRF_HPP

#include <string>

/**
 * @file sso_csrf.hpp
 * @brief Double-submit CSRF token (docs/design/sso/sso-design.md §11, Phase F).
 */
namespace sso {

/// CSRF cookie name. Matches Angular HttpClient's default XSRF cookie, so the
/// SPA echoes it as the X-XSRF-TOKEN header with no client-side code.
inline constexpr const char *kCsrfCookieName = "XSRF-TOKEN";

/**
 * @brief Build a Set-Cookie header value carrying the CSRF token.
 *
 * Deliberately NOT HttpOnly — the SPA's JavaScript must read it to echo into
 * the request header. Secure + SameSite=Lax + Path=/.
 */
std::string build_csrf_cookie(const std::string &token);

/// Extract the CSRF cookie value from a raw Cookie request header
/// ("" when absent).
std::string parse_csrf_cookie(const std::string &cookie_header);

/**
 * @brief Double-submit CSRF check.
 *
 * Safe methods (GET / HEAD / OPTIONS) always pass. A mutating request passes
 * only when @p header_token is non-empty and equals the CSRF cookie value —
 * a cross-site attacker cannot read the cookie to forge the header.
 */
bool csrf_ok(const std::string &method, const std::string &cookie_header,
             const std::string &header_token);

} // namespace sso

#endif // SSO_CSRF_HPP
