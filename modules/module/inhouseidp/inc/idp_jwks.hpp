#ifndef IDP_JWKS_HPP
#define IDP_JWKS_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "sso_endpoints.hpp"  // SsoHttpResult — shared response shape

class IMongodbClient;

/**
 * @file idp_jwks.hpp
 * @brief Build and serve the JWKS document for the in-house IdP
 *        (docs/design/inhouse-idp/inhouse-idp-design.md §3).
 *
 * Reads idp.idp_signing_keys (via IMongodbClient — the idp uniservice
 * uses the WsMongodbProxy pointed at the idp database), extracts the RSA
 * public-key parameters (n, e) from each non-expired key's PEM, and emits
 * a standard `{"keys":[{kty,kid,use,alg,n,e}, …]}` JSON document.
 */
namespace idp {

/// Minimal view of a signing key row for JWKS-building purposes.
/// (privateKeyPem is deliberately not present — JWKS is public-key-only.)
struct SigningKeyView {
  std::string  kid;
  std::string  alg;            ///< "RS256" in v1
  std::string  publicKeyPem;
  std::int64_t notAfter = 0;   ///< Unix seconds; 0 = no expiry
};

/// Build a JWKS JSON string from a list of keys.
///
/// Filters out keys whose `notAfter` has passed (`notAfter != 0 && notAfter <= now`).
/// A key whose public PEM doesn't parse is logged and skipped (no exception).
/// Empty input yields `{"keys":[]}`.
std::string jwks_from_keys(const std::vector<SigningKeyView> &keys,
                            std::int64_t now);

/// Adapter for `GET /api/v1/idp/jwks`.
///
/// Queries idp.idp_signing_keys for the active (and recently-rotated)
/// keys, hands them to `jwks_from_keys`, and wraps the body in an
/// `SsoHttpResult` with `Content-Type: application/json` and a
/// short Cache-Control.
sso::SsoHttpResult handle_idp_jwks_GET(IMongodbClient &db, std::int64_t now);

} // namespace idp

#endif // IDP_JWKS_HPP
