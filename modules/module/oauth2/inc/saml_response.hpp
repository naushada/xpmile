#ifndef SAML_RESPONSE_HPP
#define SAML_RESPONSE_HPP

#include <cstdint>
#include <string>
#include <vector>

class IMongodbClient;  // modules/module/mongodb/inc/mongodbc.hpp

/**
 * @file saml_response.hpp
 * @brief Decode, parse, and condition-check a SAML 2.0 Response
 *        (docs/design/sso/sso-design.md §5).
 */
namespace sso {

/// Fields extracted from a SAML assertion. Conditions are validated in E.4,
/// attributes mapped to IdentityClaims in E.5.
struct SamlAssertion {
  std::string id;
  std::string issuer;
  std::string subject_name_id;

  // <Conditions> and <SubjectConfirmationData>
  std::string audience;
  std::string not_before;
  std::string not_on_or_after;
  std::string recipient;
  std::string in_response_to;

  // <AttributeStatement> — mapped best-effort by attribute Name.
  std::string email;
  std::string display_name;
  std::vector<std::string> groups;
};

/// The result of decoding and parsing a base64 SAMLResponse.
struct SamlResponse {
  bool          ok = false;
  std::string   error;
  std::string   xml;        ///< decoded XML — re-parsed for XML-DSig in E.3
  SamlAssertion assertion;
};

/**
 * @brief Decode a base64 SAMLResponse, parse the XML, and extract the assertion.
 *
 * Parsing is XXE-safe — no network, no DTD load, no external-entity
 * substitution. A true @c ok means well-formed and structurally complete; it
 * does NOT mean trusted — signature verification is a separate step (E.3).
 */
SamlResponse parse_saml_response(const std::string &base64_response);

/// What the SP expects an assertion's conditions to satisfy.
struct SamlConditionExpect {
  std::string audience;   ///< our SP entity id (= spEntityId)
  std::string recipient;  ///< our Assertion Consumer Service URL
};

/// The outcome of validating an assertion's conditions.
struct SamlConditionResult {
  bool        ok = false;
  std::string error;
  std::string return_to;  ///< local path from the consumed login transaction
};

/**
 * @brief Validate a SAML assertion's conditions and atomically consume the
 *        InResponseTo login transaction.
 *
 * Checks Audience, the NotBefore/NotOnOrAfter window (±60 s skew), Recipient,
 * and that InResponseTo names a known, not-yet-consumed `sso_transactions`
 * document — which it claims single-use (a guarded update, shared with the
 * OIDC path), so a replayed assertion is rejected.
 */
SamlConditionResult validate_saml_conditions(const SamlAssertion &assertion,
                                             const SamlConditionExpect &expect,
                                             IMongodbClient &db,
                                             std::int64_t now);

} // namespace sso

#endif // SAML_RESPONSE_HPP
