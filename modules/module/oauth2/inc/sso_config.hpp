#ifndef SSO_CONFIG_HPP
#define SSO_CONFIG_HPP

#include <map>
#include <string>
#include <vector>

/**
 * @file sso_config.hpp
 * @brief Parser for the SSO_CONFIG document (see docs/design/sso/sso-design.md
 *        §10). The config is secret-bearing and supplied as a Heroku config var.
 */
namespace sso {

/// Federation protocol of a configured identity provider.
enum class Protocol { Oidc, Saml };

/**
 * @brief Configuration for one identity provider.
 *
 * OIDC- and SAML-specific fields are both present on the struct; only the set
 * relevant to @ref protocol is populated by the parser.
 */
struct ProviderConfig {
  std::string id;            ///< stable provider id (URL path segment)
  std::string display_name;  ///< shown on the login screen
  Protocol    protocol = Protocol::Oidc;

  // ── OIDC ──
  std::string issuer;        ///< discovery issuer URL
  std::string client_id;
  std::string client_secret;
  std::vector<std::string> scopes;

  // ── SAML ──
  std::string idp_entity_id;
  std::string idp_sso_url;
  std::string idp_signing_cert;  ///< PEM
  std::string sp_entity_id;

  // ── provisioning (§7) ──
  std::string default_role;
  std::vector<std::string> allowed_email_domains;
  bool        group_role_map_enabled = false;
  std::map<std::string, std::string> group_role_map;  ///< IdP group → role
};

/// Parsed top-level SSO configuration.
struct SsoConfig {
  std::string public_base_url;  ///< callback URLs are pinned to this (§10)
  std::vector<ProviderConfig> providers;
};

/**
 * @brief Parse a SSO_CONFIG JSON document.
 *
 * @param json_text  raw JSON.
 * @param out        populated on success.
 * @param error      populated with a human-readable reason on failure.
 * @return true on success, false otherwise.
 */
bool parse_sso_config(const std::string &json_text, SsoConfig &out,
                      std::string &error);

} // namespace sso

#endif // SSO_CONFIG_HPP
