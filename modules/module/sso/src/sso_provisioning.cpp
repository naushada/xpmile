#include "sso_provisioning.hpp"

#include <cctype>

#include "json.hpp"
#include "mongodbc.hpp"
#include "sso_util.hpp"

namespace sso {

namespace {

std::string to_lower(std::string s) {
  for (char &c : s)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string account_code_of(const nlohmann::json &acc) {
  if (acc.contains("loginCredentials") && acc["loginCredentials"].is_object())
    return acc["loginCredentials"].value("accountCode", std::string{});
  return {};
}

std::string role_of(const nlohmann::json &acc) {
  if (acc.contains("personalInfo") && acc["personalInfo"].is_object())
    return acc["personalInfo"].value("role", std::string{});
  return {};
}

// An empty allow-list means the provider imposes no domain restriction.
bool domain_allowed(const std::string &email,
                    const std::vector<std::string> &domains) {
  if (domains.empty()) return true;
  const std::size_t at = email.rfind('@');
  if (at == std::string::npos) return false;
  const std::string dom = to_lower(email.substr(at + 1));
  for (const auto &d : domains)
    if (to_lower(d) == dom) return true;
  return false;
}

// Role for a JIT-created account: the provider default, unless groupRoleMap
// is enabled and one of the user's IdP groups maps to a role.
std::string jit_role(const ProviderConfig &p, const IdentityClaims &claims) {
  std::string role = p.default_role.empty() ? "Customer" : p.default_role;
  if (p.group_role_map_enabled) {
    for (const auto &g : claims.groups) {
      auto it = p.group_role_map.find(g);
      if (it != p.group_role_map.end()) {
        role = it->second;
        break;
      }
    }
  }
  return role;
}

ResolvedAccount jit_create(IMongodbClient &db, const ProviderConfig &provider,
                           const IdentityClaims &claims) {
  const std::string code = "sso-" + random_token(12);
  const std::string role = jit_role(provider, claims);

  // No passwordHash — a JIT account is SSO-only until a password is set.
  nlohmann::json acc;
  acc["loginCredentials"]["accountCode"] = code;
  acc["personalInfo"]["email"]           = claims.email;
  acc["personalInfo"]["name"]            = claims.display_name;
  acc["personalInfo"]["role"]            = role;
  acc["ssoIdentities"] = nlohmann::json::array(
      {{{"provider", provider.id}, {"subject", claims.subject}}});

  db.create_document(db.get_database(), "account", acc.dump());

  ResolvedAccount out;
  out.ok           = true;
  out.account_code = code;
  out.role         = role;
  return out;
}

} // namespace

ResolvedAccount resolve_account(IMongodbClient &db,
                                const ProviderConfig &provider,
                                const IdentityClaims &claims) {
  ResolvedAccount out;

  // 1. Match by SSO subject — an account already linked to provider+subject.
  {
    nlohmann::json q;
    q["ssoIdentities"]["$elemMatch"]["provider"] = provider.id;
    q["ssoIdentities"]["$elemMatch"]["subject"]  = claims.subject;
    const std::string rec = db.get_document("account", q.dump(), "{}");
    if (!rec.empty()) {
      nlohmann::json acc =
          nlohmann::json::parse(rec, nullptr, /*allow_exceptions=*/false);
      if (!acc.is_discarded() && acc.is_object()) {
        out.ok           = true;
        out.account_code = account_code_of(acc);
        out.role         = role_of(acc);  // existing role kept, never overridden
        return out;
      }
    }
  }

  // The email-match and JIT paths are gated by the provider's domain
  // allow-list — an email outside it is rejected outright.
  if (!domain_allowed(claims.email, provider.allowed_email_domains)) {
    out.error = "email domain not permitted for this provider";
    return out;
  }

  // 2. Match by verified email — only when the IdP asserts email_verified.
  if (claims.email_verified && !claims.email.empty()) {
    nlohmann::json q;
    q["personalInfo.email"] = to_lower(claims.email);
    const std::string rec = db.get_document("account", q.dump(), "{}");
    if (!rec.empty()) {
      nlohmann::json acc =
          nlohmann::json::parse(rec, nullptr, /*allow_exceptions=*/false);
      if (!acc.is_discarded() && acc.is_object()) {
        const std::string code = account_code_of(acc);

        // Link the SSO identity so a later login matches by subject.
        nlohmann::json filter;
        filter["loginCredentials.accountCode"] = code;
        nlohmann::json upd;
        upd["$push"]["ssoIdentities"] = {{"provider", provider.id},
                                         {"subject", claims.subject}};
        db.update_collection("account", filter.dump(), upd.dump());

        out.ok           = true;
        out.account_code = code;
        out.role         = role_of(acc);
        return out;
      }
    }
  }

  // 3. Just-in-time create.
  return jit_create(db, provider, claims);
}

} // namespace sso
