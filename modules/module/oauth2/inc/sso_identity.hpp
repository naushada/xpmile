#ifndef SSO_IDENTITY_HPP
#define SSO_IDENTITY_HPP

#include <string>
#include <vector>

/**
 * @file sso_identity.hpp
 * @brief The resolved result of an SSO callback — what an identity provider
 *        produces once a login round trip completes
 *        (docs/design/sso/sso-design.md §3).
 */
namespace sso {

/// Identity resolved from a completed SSO callback, plus the flow state the
/// caller needs to finish the login.
struct IdentityClaims {
  bool        ok = false;  ///< true only when the callback verified cleanly
  std::string error;       ///< populated when ok == false

  std::string subject;     ///< IdP-stable user id (OIDC sub / SAML NameID)
  std::string email;
  bool        email_verified = false;
  std::string display_name;
  std::vector<std::string> groups;

  std::string return_to;          ///< local path to send the browser to
  std::string idp_refresh_token;  ///< optional — for RP-initiated logout
};

/// The browser redirect that begins a login, plus the id correlating the
/// eventual callback.
struct AuthnRequest {
  std::string redirect_url;    ///< value for the 302 Location header
  std::string transaction_id;  ///< correlates the callback (OIDC: == state)
};

/**
 * @brief A federated identity provider (OIDC or SAML) behind one interface.
 *
 * The SSO endpoint handlers depend only on this interface, so OIDC and SAML
 * providers are interchangeable.
 */
class IIdentityProvider {
public:
  virtual ~IIdentityProvider() = default;

  /// The configured provider id (URL path segment).
  virtual const std::string &id() const = 0;

  /// Begin a login — returns the IdP redirect.
  virtual AuthnRequest begin_login(const std::string &return_to) = 0;

  /// Complete a login from the IdP callback.
  virtual IdentityClaims handle_callback(const std::string &code,
                                         const std::string &state) = 0;
};

} // namespace sso

#endif // SSO_IDENTITY_HPP
