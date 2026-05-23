#include "sso_endpoints.hpp"

#include "json.hpp"
#include "sso_config.hpp"
#include "sso_cookie.hpp"
#include "sso_identity.hpp"
#include "sso_provisioning.hpp"
#include "sso_session.hpp"

namespace sso {

namespace {

// A return_to must be a same-origin absolute path: it starts with '/' but not
// "//" (a protocol-relative URL would let an IdP redirect off-site).
bool is_local_path(const std::string &p) {
  return p.size() >= 1 && p[0] == '/' && !(p.size() >= 2 && p[1] == '/');
}

SsoHttpResult redirect_to(const std::string &location) {
  SsoHttpResult r;
  r.status   = 302;
  r.location = location;
  return r;
}

} // namespace

SsoHttpResult sso_list_providers(const SsoConfig &config) {
  nlohmann::json arr = nlohmann::json::array();
  for (const auto &p : config.providers) {
    arr.push_back({{"id", p.id},
                   {"displayName", p.display_name},
                   {"protocol", p.protocol == Protocol::Saml ? "saml" : "oidc"}});
  }
  SsoHttpResult r;
  r.body = arr.dump();
  return r;
}

SsoHttpResult sso_begin_login(IIdentityProvider *provider,
                              const std::string &return_to) {
  SsoHttpResult r;
  if (provider == nullptr) {
    r.status = 400;
    r.body   = R"({"error":"unknown provider"})";
    return r;
  }
  if (!is_local_path(return_to)) {
    r.status = 400;
    r.body   = R"({"error":"return_to must be a local path"})";
    return r;
  }
  const AuthnRequest req = provider->begin_login(return_to);
  return redirect_to(req.redirect_url);
}

SsoHttpResult sso_complete_callback(IIdentityProvider *provider,
                                    const ProviderConfig *config,
                                    const std::string &code,
                                    const std::string &state,
                                    IMongodbClient &db, SessionManager &sm) {
  if (provider == nullptr || config == nullptr)
    return redirect_to("/login?error=unknown_provider");

  const IdentityClaims claims = provider->handle_callback(code, state);
  if (!claims.ok)
    return redirect_to("/login?error=callback_failed");

  const ResolvedAccount acc = resolve_account(db, *config, claims);
  if (!acc.ok)
    return redirect_to("/login?error=provisioning_failed");

  NewSessionParams params;
  params.account_code      = acc.account_code;
  params.role              = acc.role;
  params.auth_method       = config->protocol == Protocol::Saml
                                 ? AuthMethod::Saml
                                 : AuthMethod::Oidc;
  params.provider          = provider->id();
  params.subject           = claims.subject;
  params.idp_refresh_token = claims.idp_refresh_token;

  SsoHttpResult r = redirect_to(
      is_local_path(claims.return_to) ? claims.return_to : "/main");
  r.set_cookie = build_session_cookie(sm.create_session(params));
  return r;
}

SsoHttpResult sso_session_info(const std::string &cookie_header,
                               SessionManager &sm) {
  SsoHttpResult r;
  const AuthContext ctx = sm.lookup(parse_session_cookie(cookie_header));
  if (!ctx.valid) {
    r.status = 401;
    r.body   = R"({"error":"not authenticated"})";
    return r;
  }
  nlohmann::json body;
  body["accountCode"] = ctx.account_code;
  body["role"]        = ctx.role;
  body["authMethod"]  = auth_method_to_string(ctx.auth_method);
  r.body = body.dump();
  return r;
}

SsoHttpResult sso_logout(const std::string &cookie_header, SessionManager &sm) {
  sm.revoke(parse_session_cookie(cookie_header));
  SsoHttpResult r;
  r.body       = R"({"status":"logged out"})";
  r.set_cookie = build_expired_cookie();
  return r;
}

} // namespace sso
