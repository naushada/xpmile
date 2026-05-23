#ifndef SAML_PROVIDER_HPP
#define SAML_PROVIDER_HPP

#include <string>

#include "sso_config.hpp"
#include "sso_identity.hpp"
#include "sso_session.hpp"  // IClock

class IMongodbClient;  // modules/module/mongodb/inc/mongodbc.hpp

/**
 * @file saml_provider.hpp
 * @brief SAML 2.0 identity provider (docs/design/sso/sso-design.md §5).
 */
namespace sso {

/// Raw DEFLATE (RFC 1951, no zlib header) — the framing of the SAML
/// HTTP-Redirect binding. Returns "" on failure.
std::string saml_deflate(const std::string &data);

/// Inverse of @ref saml_deflate. Returns "" on malformed input.
std::string saml_inflate(const std::string &data);

/**
 * @brief A SAML 2.0 identity provider — SP-initiated, standard browser bindings.
 *
 * begin_login() builds an AuthnRequest and redirects to the IdP via the
 * HTTP-Redirect binding (deflate + base64). handle_callback() consumes the
 * HTTP-POST SAMLResponse. Slots in behind @ref IIdentityProvider alongside
 * OidcProvider, so adding SAML touches no callers.
 */
class SamlProvider : public IIdentityProvider {
public:
  SamlProvider(ProviderConfig config, std::string public_base_url,
               IMongodbClient &db, IClock &clock);

  SamlProvider(const SamlProvider &) = delete;
  SamlProvider &operator=(const SamlProvider &) = delete;

  /// The configured provider id (URL path segment).
  const std::string &id() const override { return m_config.id; }

  /**
   * @brief Begin a SAML login.
   *
   * Builds an AuthnRequest, persists a one-time `sso_transactions` document
   * keyed by the request ID, and returns the IdP HTTP-Redirect URL with the
   * deflated+base64 `SAMLRequest` and a `RelayState` carrying the request ID.
   */
  AuthnRequest begin_login(const std::string &return_to) override;

  /**
   * @brief Complete a SAML login from the HTTP-POST callback.
   * @param saml_response base64 `SAMLResponse` form field.
   * @param relay_state   `RelayState` form field (the request ID).
   */
  IdentityClaims handle_callback(const std::string &saml_response,
                                 const std::string &relay_state) override;

private:
  ProviderConfig  m_config;
  std::string     m_public_base_url;
  IMongodbClient &m_db;
  IClock         &m_clock;
};

} // namespace sso

#endif // SAML_PROVIDER_HPP
