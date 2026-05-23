#ifndef SSO_OIDC_HPP
#define SSO_OIDC_HPP

#include <string>

#include "sso_config.hpp"
#include "sso_http_client.hpp"
#include "sso_identity.hpp"

class IMongodbClient;  // modules/module/mongodb/inc/mongodbc.hpp

/**
 * @file sso_oidc.hpp
 * @brief OpenID Connect provider for the SSO feature
 *        (docs/design/sso/sso-design.md §4).
 */
namespace sso {

class IClock;  // sso_session.hpp — abstract time source

/**
 * @brief Make a PKCE code verifier (RFC 7636 §4.1).
 *
 * 32 bytes of CSPRNG entropy, base64url-encoded → a 43-character string drawn
 * from the PKCE unreserved set.
 */
std::string make_code_verifier();

/**
 * @brief Derive the S256 PKCE code challenge (RFC 7636 §4.2).
 * @return base64url(SHA-256(@p verifier)), no padding.
 */
std::string code_challenge(const std::string &verifier);

/// OIDC provider endpoints, from the discovery document.
struct OidcEndpoints {
  std::string issuer;
  std::string authorization_endpoint;
  std::string token_endpoint;
  std::string jwks_uri;
  std::string end_session_endpoint;  ///< optional — RP-initiated logout
};

/**
 * @brief Fetch and parse `<issuer>/.well-known/openid-configuration`.
 *
 * @param http    Outbound HTTP client.
 * @param issuer  The configured issuer URL.
 * @param out     Populated on success.
 * @param error   Populated with the reason on failure.
 * @return false on transport failure, malformed JSON, an `issuer` mismatch
 *         (OIDC Discovery §4.3), or a missing required endpoint
 *         (authorization_endpoint / token_endpoint / jwks_uri).
 */
bool fetch_discovery(IHttpClient &http, const std::string &issuer,
                     OidcEndpoints &out, std::string &error);

/**
 * @brief An OpenID Connect identity provider.
 *
 * Built from a provider's configuration and its already-fetched discovery
 * endpoints. begin_login() persists a one-time transaction and returns the
 * IdP authorization redirect. `AuthnRequest` / `IIdentityProvider` live in
 * sso_identity.hpp so OIDC and SAML share one interface.
 */
class OidcProvider : public IIdentityProvider {
public:
  OidcProvider(ProviderConfig config, OidcEndpoints endpoints,
               std::string public_base_url, IMongodbClient &db,
               IHttpClient &http, IClock &clock);

  OidcProvider(const OidcProvider &) = delete;
  OidcProvider &operator=(const OidcProvider &) = delete;

  /// The configured provider id (URL path segment).
  const std::string &id() const override { return m_config.id; }

  /**
   * @brief Begin an OIDC login (Authorization Code flow + PKCE S256).
   *
   * Generates state / nonce / PKCE verifier, persists a one-time
   * `sso_transactions` document keyed by state, and returns the
   * authorization-endpoint redirect.
   *
   * @param return_to  Local path to send the browser to after login.
   */
  AuthnRequest begin_login(const std::string &return_to) override;

  /**
   * @brief Complete an OIDC login from the IdP redirect callback.
   *
   * Atomically claims the `sso_transactions` document for @p state, exchanges
   * @p code at the token endpoint, fetches the JWKS, and verifies the returned
   * id_token (signature + iss/aud/exp/nonce). A replayed callback finds the
   * transaction already consumed and fails.
   */
  IdentityClaims handle_callback(const std::string &code,
                                 const std::string &state) override;

private:
  ProviderConfig  m_config;
  OidcEndpoints   m_endpoints;
  std::string     m_public_base_url;
  IMongodbClient &m_db;
  IHttpClient    &m_http;
  IClock         &m_clock;
};

} // namespace sso

#endif // SSO_OIDC_HPP
