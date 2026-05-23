#ifndef IDP_END_SESSION_HPP
#define IDP_END_SESSION_HPP

#include <map>
#include <string>

#include "sso_endpoints.hpp"  // sso::SsoHttpResult

class IMongodbClient;

/**
 * @file idp_end_session.hpp
 * @brief Transport-agnostic logic for POST /api/v1/idp/end_session
 *        (docs/design/inhouse-idp/inhouse-idp-design.md §3 — RP-initiated logout).
 *
 * Deletes the IdP session backing the xpmile_idp_session cookie, returns a
 * Set-Cookie that expires the cookie immediately, and 302s to the
 * post_logout_redirect_uri (which must be in the client's registered list).
 */
namespace idp {

class IdpClientRegistry;
class IdpSessionManager;

sso::SsoHttpResult end_session(
    const std::map<std::string, std::string> &query,
    const std::string &cookie_header,
    IMongodbClient &db,
    IdpClientRegistry &registry,
    IdpSessionManager &sessions);

} // namespace idp

#endif // IDP_END_SESSION_HPP
