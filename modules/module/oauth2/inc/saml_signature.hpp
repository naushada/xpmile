#ifndef SAML_SIGNATURE_HPP
#define SAML_SIGNATURE_HPP

#include <string>

/**
 * @file saml_signature.hpp
 * @brief XML-DSig verification for SAML responses
 *        (docs/design/sso/sso-design.md §5). Backed by xmlsec1 — hand-rolling
 *        XML canonicalization / signature checking is a security anti-pattern.
 */
namespace sso {

struct SamlSignatureResult {
  bool        ok = false;
  std::string error;
};

/**
 * @brief Idempotent one-time xmlsec / libxml2 initialisation.
 * @return false if xmlsec failed to initialise. Thread-safe.
 */
bool saml_crypto_init();

/**
 * @brief Verify the XML-DSig signature on a SAML Response.
 *
 * Accepts only when ALL of the following hold:
 *  - exactly one @c <Signature> and exactly one @c <Assertion> are present
 *    (a signature-wrapping decoy shows up as an extra element);
 *  - the signature verifies against @p idp_cert_pem — the ONLY trusted key;
 *    any @c <KeyInfo> embedded in the response is ignored;
 *  - the signed element (the @c <Reference> URI) is that assertion or the
 *    response root.
 *
 * @param xml          the decoded SAML Response XML (not base64).
 * @param idp_cert_pem the IdP signing certificate, PEM-encoded.
 */
SamlSignatureResult verify_saml_signature(const std::string &xml,
                                          const std::string &idp_cert_pem);

} // namespace sso

#endif // SAML_SIGNATURE_HPP
