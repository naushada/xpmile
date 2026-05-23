#ifndef IDP_DISCOVERY_HPP
#define IDP_DISCOVERY_HPP

#include <string>

#include "sso_endpoints.hpp"  // SsoHttpResult

/**
 * @file idp_discovery.hpp
 * @brief OIDC discovery document for the in-house IdP
 *        (docs/design/inhouse-idp/inhouse-idp-design.md §3).
 *
 * The discovery document lives at
 *   <issuer>/.well-known/openid-configuration
 * and lists the URL of every other endpoint plus the algorithms / scopes
 * / claims the IdP supports. Standard OIDC client libraries fetch it once
 * (and cache it) to bootstrap the federation.
 */
namespace idp {

/// Build the OIDC discovery JSON for the given issuer URL.
///
/// All endpoint URLs in the doc are absolute, derived from @p issuer.
/// The static metadata reflects the design's v1 scope:
///   - response_types_supported:                  ["code"]
///   - id_token_signing_alg_values_supported:     ["RS256"]
///   - code_challenge_methods_supported:          ["S256"]
///   - token_endpoint_auth_methods_supported:     ["client_secret_post"]
///   - scopes_supported:                          ["openid","email","profile"]
///   - claims_supported:                          ["sub","iss","aud","exp","iat","nonce","email","name","role"]
///   - subject_types_supported:                   ["public"]
std::string build_discovery(const std::string &issuer);

/// Adapter for `GET /api/v1/idp/.well-known/openid-configuration`.
sso::SsoHttpResult handle_idp_discovery_GET(const std::string &issuer);

} // namespace idp

#endif // IDP_DISCOVERY_HPP
