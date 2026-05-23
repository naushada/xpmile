#ifndef SSO_ENDPOINTS_HPP
#define SSO_ENDPOINTS_HPP

#include <string>

class IMongodbClient;  // modules/module/mongodb/inc/mongodbc.hpp

/**
 * @file sso_endpoints.hpp
 * @brief Transport-agnostic logic for the SSO HTTP endpoints under
 *        `/api/v1/sso/` (docs/design/sso/sso-design.md §8).
 *
 * These functions own the SSO endpoint behaviour but know nothing about ACE
 * sockets or the `Http` parser — they take their dependencies explicitly and
 * return a `SsoHttpResult`. `webservice.cpp` adapts each into an
 * `Http`-shaped response, which keeps this logic unit-testable with mocks.
 */
namespace sso {

class IIdentityProvider;
class SessionManager;
struct ProviderConfig;
struct SsoConfig;

/// The outcome of an SSO endpoint, before it is rendered onto the wire.
struct SsoHttpResult {
  int         status       = 200;
  std::string content_type = "application/json";
  std::string body;
  std::string location;    ///< Location header value when status is 302
  std::string set_cookie;  ///< Set-Cookie header value, empty if none
};

/// GET /api/v1/sso/providers — the configured providers for the login screen.
SsoHttpResult sso_list_providers(const SsoConfig &config);

/// GET /api/v1/sso/login — 302 to the IdP, or 400 for an unknown provider /
/// non-local `return_to`. @p provider is null when the id did not resolve.
SsoHttpResult sso_begin_login(IIdentityProvider *provider,
                              const std::string &return_to);

/// GET /api/v1/sso/callback/<id> — verify the callback, provision the account,
/// mint a session, and 302 back into the app. On any failure, 302 to /login
/// with an error and no session.
SsoHttpResult sso_complete_callback(IIdentityProvider *provider,
                                    const ProviderConfig *config,
                                    const std::string &code,
                                    const std::string &state,
                                    IMongodbClient &db, SessionManager &sm);

/// GET /api/v1/sso/session — the account behind the session cookie, or 401.
SsoHttpResult sso_session_info(const std::string &cookie_header,
                               SessionManager &sm);

/// POST /api/v1/sso/logout — revoke the session and expire the cookie.
SsoHttpResult sso_logout(const std::string &cookie_header, SessionManager &sm);

} // namespace sso

#endif // SSO_ENDPOINTS_HPP
