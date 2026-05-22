#ifndef SSO_SESSION_HPP
#define SSO_SESSION_HPP

#include <cstdint>
#include <list>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "sso_cookie.hpp"  // kDefaultSessionMaxAge

/**
 * @file sso_session.hpp
 * @brief Server-side session store for the SSO feature
 *        (docs/design/sso/sso-design.md §2).
 *
 * A session is an ordinary document in the @c sessions MongoDB collection,
 * keyed by an opaque random id (the cookie value). The store is reached
 * through @c IMongodbClient, so it works identically in direct and
 * @c --remote-db modes.
 */

class IMongodbClient;  // modules/module/mongodb/inc/mongodbc.hpp

namespace sso {

/// How the user authenticated for a given session.
enum class AuthMethod { Password, Oidc, Saml };

std::string auth_method_to_string(AuthMethod m);
AuthMethod  auth_method_from_string(const std::string &s);

/// Resolved identity for an authenticated request.
struct AuthContext {
  bool        valid = false;
  std::string account_code;
  std::string role;
  AuthMethod  auth_method = AuthMethod::Password;
};

/// Inputs needed to mint a new session.
struct NewSessionParams {
  std::string account_code;
  std::string role;
  AuthMethod  auth_method = AuthMethod::Password;
  std::string provider;           ///< configured provider id; "" for password
  std::string subject;            ///< IdP subject/NameID; "" for password
  std::string idp_refresh_token;  ///< optional; SSO only
};

/// Abstract time source — injectable so expiry logic is deterministic in tests.
class IClock {
public:
  virtual ~IClock() = default;
  /// Current time as Unix epoch seconds.
  virtual std::int64_t now_unix() const = 0;
};

/// Production clock backed by the system wall clock.
class SystemClock : public IClock {
public:
  std::int64_t now_unix() const override;
};

/**
 * @brief Bounded, TTL'd, thread-safe LRU cache of resolved sessions.
 *
 * Sits in front of the DB lookup so a hot session resolves without a round
 * trip to MongoDB (which, in --remote-db mode, also avoids a wsdbagent hop).
 * Coherent only within a single process — see sso-design.md §2.
 */
class SessionCache {
public:
  explicit SessionCache(IClock &clock, std::int64_t ttl_secs = 60,
                        std::size_t capacity = 4096);

  SessionCache(const SessionCache &) = delete;
  SessionCache &operator=(const SessionCache &) = delete;

  /// Insert or refresh an entry.
  void put(const std::string &sid, const AuthContext &ctx);

  /// Return the cached context, or nullopt on a miss or TTL expiry.
  std::optional<AuthContext> get(const std::string &sid);

  /// Remove an entry immediately — used by revoke() so a logout takes effect
  /// without waiting out the TTL.
  void erase(const std::string &sid);

  /// Current number of cached entries (test/observability helper).
  std::size_t size() const;

private:
  struct Entry {
    AuthContext                      ctx;
    std::int64_t                     inserted_at = 0;
    std::list<std::string>::iterator lru_it;
  };

  IClock      &m_clock;
  std::int64_t m_ttl;
  std::size_t  m_capacity;

  mutable std::mutex                     m_mtx;
  std::unordered_map<std::string, Entry> m_map;
  std::list<std::string>                 m_lru;  // front = most-recently used
};

/**
 * @brief Create, look up, and revoke server-side sessions.
 *
 * One long-lived instance is shared across MicroService worker threads; the
 * @c lastSeenAt write-throttle is mutex-guarded for that reason.
 */
class SessionManager {
public:
  SessionManager(IMongodbClient &db, IClock &clock,
                 long max_age = kDefaultSessionMaxAge);

  SessionManager(const SessionManager &) = delete;
  SessionManager &operator=(const SessionManager &) = delete;

  /// Write a new session document and return its opaque id (the cookie value).
  std::string create_session(const NewSessionParams &params);

  /// Resolve a session id. Returns an invalid AuthContext when the id is
  /// unknown or the session has expired.
  AuthContext lookup(const std::string &sid);

  /// Delete the session document (logout / forced revocation).
  void revoke(const std::string &sid);

private:
  // Refresh lastSeenAt at most once per minute per sid (in-process throttle).
  void maybe_refresh_last_seen(const std::string &sid);

  IMongodbClient &m_db;
  IClock         &m_clock;
  long            m_max_age;
  SessionCache    m_cache;

  std::mutex                          m_refresh_mtx;
  std::map<std::string, std::int64_t> m_last_refresh;
};

} // namespace sso

#endif // SSO_SESSION_HPP
