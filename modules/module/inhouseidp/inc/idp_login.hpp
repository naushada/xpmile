#ifndef IDP_LOGIN_HPP
#define IDP_LOGIN_HPP

#include <string>

#include "sso_endpoints.hpp"  // sso::SsoHttpResult
#include "sso_session.hpp"     // sso::IClock

class IMongodbClient;

/**
 * @file idp_login.hpp
 * @brief Transport-agnostic logic for POST /api/v1/idp/login — the
 *        credential check invoked by the branded portal form
 *        (docs/design/inhouse-idp/inhouse-idp-design.md §3).
 *
 * Reads the xpmile_idp_pending cookie, resolves the pending_auth doc,
 * checks the credentials against idp.account via the supplied
 * IPasswordVerifier, mints an IdP session, and 302s back to
 * /api/v1/idp/authorize?... with the original parameters.
 *
 * Returns 401 with `invalid_credentials` on wrong password — same shape
 * whether the account exists or not (no enumeration).
 */
namespace idp {

class IdpSessionManager;
class IPasswordVerifier;

sso::SsoHttpResult login(
    const std::string &user,
    const std::string &password,
    const std::string &cookie_header,
    IMongodbClient &db,
    IdpSessionManager &sessions,
    IPasswordVerifier &verifier,
    sso::IClock &clock);

} // namespace idp

#endif // IDP_LOGIN_HPP
