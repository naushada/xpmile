#ifndef IDP_SESSION_HPP
#define IDP_SESSION_HPP

#include <cstdint>
#include <string>

#include "sso_session.hpp"  // sso::IClock — reused from v1.0 SSO

class IMongodbClient;

/**
 * @file idp_session.hpp
 * @brief Server-side IdP sessions for the in-house OIDC IdP.
 *
 * Distinct from the v1.0 sso::SessionManager (which manages the RP-side
 * xpmile_session cookie). The IdP session — xpmile_idp_session, scoped to
 * /api/v1/idp/ — is what /authorize uses to decide "is this browser
 * already authenticated to the IdP?" without prompting for credentials
 * again. Stored in idp.sessions (the same physical collection name as
 * v1.0 SSO uses, but in the separate idp database — see design §1).
 */
namespace idp {

/// Identity resolved from a valid IdP session cookie.
struct IdpAuthContext {
  bool        valid = false;
  std::string sid;
  std::string accountCode;
  std::string email;
  std::string name;
  std::string role;
};

/// Inputs needed to mint a new IdP session row.
struct NewIdpSessionParams {
  std::string accountCode;
  std::string email;
  std::string name;
  std::string role;
};

class IdpSessionManager {
public:
  static constexpr const char *kDb            = "idp";
  static constexpr const char *kColl          = "sessions";
  static constexpr long        kDefaultMaxAge = 43200;  // 12 hours

  IdpSessionManager(IMongodbClient &db, sso::IClock &clock,
                    long max_age = kDefaultMaxAge);

  IdpSessionManager(const IdpSessionManager &) = delete;
  IdpSessionManager &operator=(const IdpSessionManager &) = delete;

  /// Create a session row in idp.sessions and return its opaque sid
  /// (32 bytes of CSPRNG, base64url-encoded). Returns "" on RNG failure.
  std::string create(const NewIdpSessionParams &params);

  /// Resolve a session id. Returns an invalid context on missing /
  /// expired / malformed records.
  IdpAuthContext lookup(const std::string &sid);

  /// Delete a single session row (logout / forced revocation).
  void revoke(const std::string &sid);

  /// Delete every session row for the given accountCode (used by the
  /// password-reset flow so a successful reset kills outstanding
  /// IdP sessions and forces a re-auth at the next /authorize).
  void revoke_all_for_account(const std::string &accountCode);

private:
  IMongodbClient &m_db;
  sso::IClock    &m_clock;
  long            m_max_age;
};

} // namespace idp

#endif // IDP_SESSION_HPP
