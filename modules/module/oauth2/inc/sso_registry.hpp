#ifndef SSO_REGISTRY_HPP
#define SSO_REGISTRY_HPP

#include <string>

#include "sso_config.hpp"

/**
 * @file sso_registry.hpp
 * @brief Holds the live SSO configuration and hot-reloads it from the raw
 *        `sso_config` document (docs/design/sso/sso-design.md §10).
 */
namespace sso {

/**
 * @brief The live SSO provider configuration, hot-reloadable.
 *
 * A reload that fails to parse is rejected: the last-good configuration is
 * kept, so a bad operator edit in the Vaadin admin UI can never take down
 * login.
 */
class ProviderRegistry {
public:
  /**
   * @brief Reload from a raw `sso_config` JSON document.
   * @return true only when a new, valid configuration was swapped in;
   *         false when the document is unchanged, or changed but invalid.
   */
  bool reload_if_changed(const std::string &raw_json);

  /// The current (last-good) configuration.
  const SsoConfig &config() const { return m_config; }

  /// Resolve a configured provider by id; nullptr when not present.
  const ProviderConfig *find(const std::string &provider_id) const;

private:
  SsoConfig   m_config;
  std::string m_hash;  ///< SHA-256 of the last document processed
};

} // namespace sso

#endif // SSO_REGISTRY_HPP
