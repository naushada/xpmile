#ifndef SSO_PROVISIONING_HPP
#define SSO_PROVISIONING_HPP

#include <string>

#include "sso_config.hpp"
#include "sso_identity.hpp"

class IMongodbClient;  // modules/module/mongodb/inc/mongodbc.hpp

/**
 * @file sso_provisioning.hpp
 * @brief Hybrid account provisioning for the SSO feature
 *        (docs/design/sso/sso-design.md §7).
 */
namespace sso {

/// The xpmile account an SSO identity resolved to.
struct ResolvedAccount {
  bool        ok = false;
  std::string error;
  std::string account_code;
  std::string role;
};

/**
 * @brief Resolve a verified SSO identity to an xpmile account.
 *
 * Hybrid strategy: (1) match an account already linked to this
 * provider+subject; (2) otherwise, within the provider's allowed email
 * domains, match by verified email and link the identity; (3) otherwise
 * just-in-time create an account. An email outside the provider's
 * `allowedEmailDomains` is rejected outright.
 *
 * A matched account keeps its existing DB role — the IdP never overrides it.
 */
ResolvedAccount resolve_account(IMongodbClient &db,
                                const ProviderConfig &provider,
                                const IdentityClaims &claims);

} // namespace sso

#endif // SSO_PROVISIONING_HPP
