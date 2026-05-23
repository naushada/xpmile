#ifndef IDP_JWT_SIGNER_HPP
#define IDP_JWT_SIGNER_HPP

#include <string>

/**
 * @file jwt_signer.hpp
 * @brief Abstract JWT signing interface for the in-house OIDC IdP.
 *
 * The private signing key never leaves the on-prem MongoDB — the production
 * implementation (WsdbJwtSigner) delegates the actual sign operation to
 * wsdbagent over the new SIGN_JWT dbproto op (design §2). Tests use a mock
 * implementation so they can run inside the offtarget binary without a
 * running cloud or real RSA crypto.
 */
namespace idp {

/// Result of an asymmetric JWT signing operation.
struct SignJwtResult {
  bool        ok = false;
  std::string error;       ///< Populated when ok == false.
  std::string signature;   ///< base64url-encoded signature (RFC 7515 §2, no padding).
  std::string kid;         ///< The kid actually used (resolves "current" → real kid).
};

/// Abstract signer — what the IdP's /token handler depends on.
///
/// Implementations:
///   - `WsdbJwtSigner`  — production: delegates to wsdbagent via SIGN_JWT.
///   - `MockJwtSigner`  — tests: returns a deterministic fake signature.
class IJwtSigner {
public:
  virtual ~IJwtSigner() = default;

  /// Sign @p to_sign with the signing key identified by @p kid_hint.
  ///
  /// @param kid_hint  `"current"` to use the active key, or a specific kid.
  /// @param alg       JWT algorithm (only `"RS256"` is supported in v1).
  /// @param to_sign   The JWS to-be-signed bytes: `b64u(header) + "." + b64u(payload)`.
  /// @return SignJwtResult with the base64url-encoded signature on success,
  ///         or with `ok=false` and a human-readable @ref error on failure.
  virtual SignJwtResult sign(const std::string &kid_hint,
                              const std::string &alg,
                              const std::string &to_sign) = 0;
};

} // namespace idp

#endif // IDP_JWT_SIGNER_HPP
