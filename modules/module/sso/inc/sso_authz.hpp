#ifndef SSO_AUTHZ_HPP
#define SSO_AUTHZ_HPP

#include <string>

/**
 * @file sso_authz.hpp
 * @brief Endpoint auth-enforcement predicate (Phase F.2,
 *        docs/design/sso/sso-design.md §14 Q1).
 *
 * This is the decision logic only. Wiring it into process_request as live
 * 401 enforcement is deferred — blanket enforcement needs a full caller audit
 * and an auth path for the on-prem Vaadin console (§14 Q1).
 */
namespace sso {

/**
 * @brief Does reaching this request URI require a valid session?
 *
 * Exempt: non-`/api/v1/` routes (static assets), the password-login endpoint,
 * and the SSO flow — `/api/v1/sso/` paths run before a session exists, since a
 * session is their output. Every other `/api/v1/` route is protected.
 */
bool sso_requires_session(const std::string &uri);

/**
 * @brief The auth-enforcement decision.
 * @return true to allow the request; false means a protected endpoint was
 *         reached without a valid session and must be rejected with 401.
 */
bool sso_authorize(const std::string &uri, bool has_valid_session);

} // namespace sso

#endif // SSO_AUTHZ_HPP
