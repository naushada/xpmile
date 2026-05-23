#ifndef IDP_AUTHORIZE_HPP
#define IDP_AUTHORIZE_HPP

#include <map>
#include <string>

#include "sso_endpoints.hpp"  // sso::SsoHttpResult
#include "sso_session.hpp"     // sso::IClock

class IMongodbClient;

/**
 * @file idp_authorize.hpp
 * @brief Transport-agnostic logic for the OIDC /authorize endpoint
 *        (docs/design/inhouse-idp/inhouse-idp-design.md §3).
 *
 * Validates the query params (client_id known + redirect_uri exact match
 * via the registry; response_type=code; PKCE S256; openid scope; state
 * present), then either:
 *   - persists a one-time `idp_pending_auth` document and 302s to /login
 *     (no IdP session yet — sets the xpmile_idp_pending cookie), or
 *   - mints a one-time authorization code in `idp_codes` and 302s back
 *     to the RP's redirect_uri (valid IdP session present).
 *
 * Negative paths return 4xx with an OAuth2-style {error,error_description}.
 */
namespace idp {

class IdpClientRegistry;
class IdpSessionManager;

sso::SsoHttpResult authorize(
    const std::map<std::string, std::string> &query,
    const std::string &cookie_header,
    IMongodbClient &db,
    IdpClientRegistry &registry,
    IdpSessionManager &sessions,
    sso::IClock &clock);

} // namespace idp

#endif // IDP_AUTHORIZE_HPP
