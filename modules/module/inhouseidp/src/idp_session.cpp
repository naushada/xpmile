#include "idp_session.hpp"

#include "json.hpp"
#include "mongodbc.hpp"
#include "sso_util.hpp"  // sso::random_token

namespace idp {

IdpSessionManager::IdpSessionManager(IMongodbClient &db, sso::IClock &clock,
                                     long max_age)
    : m_db(db), m_clock(clock), m_max_age(max_age) {}

std::string IdpSessionManager::create(const NewIdpSessionParams &params) {
  const std::string sid = sso::random_token(32);
  if (sid.empty()) return {};

  const std::int64_t now = m_clock.now_unix();
  nlohmann::json doc = {
      {"_id",         sid},
      {"accountCode", params.accountCode},
      {"email",       params.email},
      {"name",        params.name},
      {"role",        params.role},
      {"createdAt",   now},
      {"lastSeenAt",  now},
      {"expiresAt",   now + m_max_age},
  };
  m_db.create_document(kDb, kColl, doc.dump());
  return sid;
}

IdpAuthContext IdpSessionManager::lookup(const std::string &sid) {
  IdpAuthContext ctx;
  if (sid.empty()) return ctx;

  const nlohmann::json query = {{"_id", sid}};
  const std::string raw = m_db.get_document(kColl, query.dump(),
                                              /*projection=*/"{}");
  if (raw.empty()) return ctx;

  nlohmann::json doc;
  try {
    doc = nlohmann::json::parse(raw);
  } catch (const std::exception &) {
    return ctx;
  }

  const std::int64_t now = m_clock.now_unix();
  const std::int64_t expires_at = doc.value("expiresAt", std::int64_t{0});
  if (expires_at > 0 && now >= expires_at) return ctx;

  ctx.valid       = true;
  ctx.sid         = sid;
  ctx.accountCode = doc.value("accountCode", std::string{});
  ctx.email       = doc.value("email",       std::string{});
  ctx.name        = doc.value("name",        std::string{});
  ctx.role        = doc.value("role",        std::string{});
  return ctx;
}

void IdpSessionManager::revoke(const std::string &sid) {
  if (sid.empty()) return;
  const nlohmann::json filter = {{"_id", sid}};
  m_db.delete_document(kColl, filter.dump());
}

void IdpSessionManager::revoke_all_for_account(const std::string &accountCode) {
  if (accountCode.empty()) return;
  const nlohmann::json filter = {{"accountCode", accountCode}};
  m_db.delete_document(kColl, filter.dump());
}

} // namespace idp
