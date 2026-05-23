#include "sso_session.hpp"

#include <chrono>
#include <utility>

#include "json.hpp"
#include "mongodbc.hpp"
#include "sso_util.hpp"

namespace sso {

namespace {

constexpr const char *kSessionsCollection = "sessions";

// lastSeenAt is refreshed at most once per this many seconds, per session.
constexpr std::int64_t kLastSeenThrottleSecs = 60;

} // namespace

std::string auth_method_to_string(AuthMethod m) {
  switch (m) {
    case AuthMethod::Oidc: return "oidc";
    case AuthMethod::Saml: return "saml";
    case AuthMethod::Password:
    default:               return "password";
  }
}

AuthMethod auth_method_from_string(const std::string &s) {
  if (s == "oidc") return AuthMethod::Oidc;
  if (s == "saml") return AuthMethod::Saml;
  return AuthMethod::Password;
}

std::int64_t SystemClock::now_unix() const {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

SessionManager::SessionManager(IMongodbClient &db, IClock &clock, long max_age)
    : m_db(db), m_clock(clock), m_max_age(max_age), m_cache(clock) {}

std::string SessionManager::create_session(const NewSessionParams &params) {
  // 32 bytes of CSPRNG entropy, base64url-encoded → a 43-char opaque id.
  const std::string sid  = random_token(32);
  const std::int64_t now = m_clock.now_unix();

  // Time fields are stored as Unix epoch seconds: the security-critical expiry
  // check happens at lookup time on this value. A MongoDB TTL index (which
  // needs a BSON Date) is a separate cleanup concern, wired in mongo-init.js.
  nlohmann::json doc;
  doc["_id"]         = sid;
  doc["accountCode"] = params.account_code;
  doc["role"]        = params.role;
  doc["authMethod"]  = auth_method_to_string(params.auth_method);
  doc["provider"]    = params.provider;
  doc["subject"]     = params.subject;
  doc["createdAt"]   = now;
  doc["lastSeenAt"]  = now;
  doc["expiresAt"]   = now + m_max_age;
  if (!params.idp_refresh_token.empty())
    doc["idpRefreshToken"] = params.idp_refresh_token;

  m_db.create_document(m_db.get_database(), kSessionsCollection, doc.dump());
  return sid;
}

AuthContext SessionManager::lookup(const std::string &sid) {
  AuthContext ctx;
  if (sid.empty()) return ctx;

  if (std::optional<AuthContext> cached = m_cache.get(sid))
    return *cached;

  nlohmann::json query;
  query["_id"] = sid;
  const std::string raw =
      m_db.get_document(kSessionsCollection, query.dump(), "{}");
  if (raw.empty()) return ctx;  // unknown session

  nlohmann::json j =
      nlohmann::json::parse(raw, nullptr, /*allow_exceptions=*/false);
  if (j.is_discarded() || !j.is_object()) return ctx;

  const std::int64_t expires = j.value("expiresAt", std::int64_t{0});
  if (expires <= m_clock.now_unix()) return ctx;  // expired

  ctx.valid        = true;
  ctx.account_code = j.value("accountCode", std::string{});
  ctx.role         = j.value("role", std::string{});
  ctx.auth_method =
      auth_method_from_string(j.value("authMethod", std::string{"password"}));

  m_cache.put(sid, ctx);
  maybe_refresh_last_seen(sid);
  return ctx;
}

void SessionManager::revoke(const std::string &sid) {
  if (sid.empty()) return;
  m_cache.erase(sid);  // synchronous purge — logout must take effect at once
  nlohmann::json filter;
  filter["_id"] = sid;
  m_db.delete_document(kSessionsCollection, filter.dump());
}

void SessionManager::maybe_refresh_last_seen(const std::string &sid) {
  const std::int64_t now = m_clock.now_unix();
  {
    std::lock_guard<std::mutex> lock(m_refresh_mtx);
    auto it = m_last_refresh.find(sid);
    if (it != m_last_refresh.end() && now - it->second < kLastSeenThrottleSecs)
      return;
    m_last_refresh[sid] = now;
  }
  nlohmann::json filter;
  filter["_id"] = sid;
  nlohmann::json update;
  update["$set"]["lastSeenAt"] = now;
  m_db.update_collection(kSessionsCollection, filter.dump(), update.dump());
}

// ── SessionCache ──────────────────────────────────────────────────────────

SessionCache::SessionCache(IClock &clock, std::int64_t ttl_secs,
                           std::size_t capacity)
    : m_clock(clock), m_ttl(ttl_secs), m_capacity(capacity) {}

void SessionCache::put(const std::string &sid, const AuthContext &ctx) {
  std::lock_guard<std::mutex> lock(m_mtx);
  const std::int64_t now = m_clock.now_unix();

  auto it = m_map.find(sid);
  if (it != m_map.end()) {
    it->second.ctx         = ctx;
    it->second.inserted_at = now;
    m_lru.erase(it->second.lru_it);
    m_lru.push_front(sid);
    it->second.lru_it = m_lru.begin();
    return;
  }

  m_lru.push_front(sid);
  Entry e;
  e.ctx         = ctx;
  e.inserted_at = now;
  e.lru_it      = m_lru.begin();
  m_map.emplace(sid, std::move(e));

  while (m_map.size() > m_capacity && !m_lru.empty()) {
    const std::string victim = m_lru.back();  // least-recently used
    m_lru.pop_back();
    m_map.erase(victim);
  }
}

std::optional<AuthContext> SessionCache::get(const std::string &sid) {
  std::lock_guard<std::mutex> lock(m_mtx);
  auto it = m_map.find(sid);
  if (it == m_map.end()) return std::nullopt;

  if (m_clock.now_unix() - it->second.inserted_at >= m_ttl) {
    m_lru.erase(it->second.lru_it);
    m_map.erase(it);
    return std::nullopt;
  }

  // LRU touch — move to the front.
  m_lru.erase(it->second.lru_it);
  m_lru.push_front(sid);
  it->second.lru_it = m_lru.begin();
  return it->second.ctx;
}

void SessionCache::erase(const std::string &sid) {
  std::lock_guard<std::mutex> lock(m_mtx);
  auto it = m_map.find(sid);
  if (it == m_map.end()) return;
  m_lru.erase(it->second.lru_it);
  m_map.erase(it);
}

std::size_t SessionCache::size() const {
  std::lock_guard<std::mutex> lock(m_mtx);
  return m_map.size();
}

} // namespace sso
