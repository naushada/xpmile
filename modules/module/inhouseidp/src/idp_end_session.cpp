#include "idp_end_session.hpp"

#include "idp_client_registry.hpp"
#include "idp_cookie.hpp"
#include "idp_session.hpp"
#include "json.hpp"
#include "mongodbc.hpp"

namespace idp {

namespace {

const std::string &get(const std::map<std::string, std::string> &m,
                        const std::string &k) {
  static const std::string empty;
  auto it = m.find(k);
  return (it == m.end()) ? empty : it->second;
}

sso::SsoHttpResult error_response(int status, const std::string &msg) {
  sso::SsoHttpResult r;
  r.status       = status;
  r.content_type = "application/json";
  nlohmann::json body = {{"error", "invalid_request"}, {"error_description", msg}};
  r.body = body.dump();
  return r;
}

} // namespace

sso::SsoHttpResult end_session(
    const std::map<std::string, std::string> &query,
    const std::string &cookie_header,
    IMongodbClient &db,
    IdpClientRegistry &registry,
    IdpSessionManager &sessions) {

  (void)db;  // IdpSessionManager owns the db handle for the actual revoke.

  const std::string &client_id   = get(query, "client_id");
  const std::string &post_logout = get(query, "post_logout_redirect_uri");

  // post_logout_redirect_uri is optional in OIDC, but when present it MUST
  // be exactly registered with the named client. (Open redirect defence.)
  if (!post_logout.empty()) {
    if (client_id.empty()) {
      return error_response(400, "client_id required when "
                                  "post_logout_redirect_uri is present");
    }
    if (!registry.validate_post_logout_redirect_uri(client_id, post_logout)) {
      return error_response(400,
                              "post_logout_redirect_uri not registered for client");
    }
  }

  // Delete the IdP session row (best effort — sid from the cookie).
  const std::string sid = parse_cookie(cookie_header, kIdpSessionCookieName);
  if (!sid.empty()) {
    sessions.revoke(sid);
  }

  sso::SsoHttpResult r;
  r.status     = 302;
  r.location   = post_logout.empty() ? std::string{"/login"} : post_logout;
  r.set_cookie = build_expired_cookie(kIdpSessionCookieName);
  return r;
}

} // namespace idp
