#ifndef IDP_CLIENT_REGISTRY_HPP
#define IDP_CLIENT_REGISTRY_HPP

#include <cstdint>
#include <string>
#include <vector>

class IMongodbClient;

/**
 * @file idp_client_registry.hpp
 * @brief Registered OIDC relying-party clients for the in-house IdP
 *        (docs/design/inhouse-idp/inhouse-idp-design.md §3 — /authorize).
 *
 * Loaded from idp.idp_clients (via IMongodbClient::get_documents on the
 * idp uniservice's proxy). One row per registered client. v1 ships with
 * a single registered client — `xpmile-spa` — pre-seeded via the
 * IdpClientsView Vaadin admin (Phase J).
 *
 * Redirect URI matching is intentionally **exact byte equality**: no
 * trailing-slash tolerance, no query-string stripping, no case folding.
 * The /authorize handler is the only point that validates redirect_uri,
 * so getting it right here matters.
 */
namespace idp {

struct IdpClient {
  std::string              clientId;
  std::string              clientName;
  std::string              clientSecretHash;        ///< stored hashed; never compared as plaintext
  std::vector<std::string> redirectUris;            ///< exact-match list
  std::vector<std::string> postLogoutRedirectUris;  ///< exact-match list
  std::vector<std::string> scopes;                  ///< granted scopes
  std::vector<std::string> grantTypes;              ///< typically ["authorization_code"]
};

class IdpClientRegistry {
public:
  static constexpr const char *kColl = "idp_clients";

  /// Replace the in-memory cache with the contents of `idp.idp_clients`.
  /// Returns the number of valid clients loaded. A malformed document is
  /// logged and skipped (no exception escapes).
  std::int32_t reload(IMongodbClient &db);

  /// Find a registered client by id; nullptr when absent.
  const IdpClient *find(const std::string &client_id) const;

  /// Exact-byte match: is `redirect_uri` in the client's registered list?
  /// Returns false if the client is unknown.
  bool validate_redirect_uri(const std::string &client_id,
                              const std::string &redirect_uri) const;

  /// Same, for the post-logout list (used by /end_session).
  bool validate_post_logout_redirect_uri(const std::string &client_id,
                                          const std::string &uri) const;

  std::size_t size() const { return m_clients.size(); }

private:
  std::vector<IdpClient> m_clients;
};

} // namespace idp

#endif // IDP_CLIENT_REGISTRY_HPP
