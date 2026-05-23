#ifndef IDP_COOKIE_HPP
#define IDP_COOKIE_HPP

#include <string>

/**
 * @file idp_cookie.hpp
 * @brief Cookie helpers for the in-house IdP host.
 *
 * Distinct from the v1.0 sso_cookie.hpp helpers (which only know about the
 * RP-side `xpmile_session` cookie). The IdP host serves two cookies:
 *   - `xpmile_idp_session`   the long-lived IdP session
 *   - `xpmile_idp_pending`   the short-lived pending-auth correlator
 * Both are scoped to Path=/api/v1/idp/ so they're only sent to the IdP
 * routes (never leak to the RP).
 */
namespace idp {

inline constexpr const char *kIdpSessionCookieName = "xpmile_idp_session";
inline constexpr const char *kIdpPendingCookieName = "xpmile_idp_pending";

inline constexpr long kDefaultIdpSessionMaxAge = 43200;  ///< 12 h
inline constexpr long kDefaultIdpPendingMaxAge = 600;    ///< 10 min

/// Build a Set-Cookie header value for the IdP session cookie.
/// `HttpOnly; Secure; SameSite=Lax; Path=/api/v1/idp/`.
std::string build_idp_session_cookie(const std::string &sid,
                                      long max_age = kDefaultIdpSessionMaxAge);

/// Same shape, for the short-lived pending-auth cookie.
std::string build_idp_pending_cookie(const std::string &req_token,
                                      long max_age = kDefaultIdpPendingMaxAge);

/// A Set-Cookie that expires the named cookie immediately (Max-Age=0).
std::string build_expired_cookie(const std::string &name);

/// Extract `name`'s value from a raw `Cookie:` request header.
/// Returns "" when the cookie is absent or the header is malformed.
std::string parse_cookie(const std::string &cookie_header,
                          const std::string &name);

} // namespace idp

#endif // IDP_COOKIE_HPP
