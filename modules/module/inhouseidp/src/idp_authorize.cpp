#include "idp_authorize.hpp"

#include <iomanip>
#include <sstream>

#include "idp_client_registry.hpp"
#include "idp_cookie.hpp"
#include "idp_session.hpp"
#include "json.hpp"
#include "mongodbc.hpp"
#include "sso_util.hpp"  // sso::random_token

namespace idp {

namespace {

inline constexpr long kPendingMaxAgeSec = 600;  // 10 min — must outlive the round trip to /login
inline constexpr long kCodeMaxAgeSec    = 30;   // 30 s — OIDC authorization codes are short-lived

const std::string &get(const std::map<std::string, std::string> &q,
                        const std::string &key) {
  static const std::string empty;
  auto it = q.find(key);
  return (it == q.end()) ? empty : it->second;
}

sso::SsoHttpResult error_400(const std::string &error,
                              const std::string &description = {}) {
  sso::SsoHttpResult r;
  r.status       = 400;
  r.content_type = "application/json";
  nlohmann::json body = {{"error", error}};
  if (!description.empty()) body["error_description"] = description;
  r.body = body.dump();
  return r;
}

// RFC 3986 unreserved-set URL encoding. Matches the existing v1.0 SSO
// encode_form() output for OAuth2 redirects.
std::string url_encode(const std::string &s) {
  std::ostringstream os;
  os << std::hex << std::uppercase;
  for (unsigned char c : s) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' ||
        c == '.' || c == '~') {
      os << static_cast<char>(c);
    } else {
      os << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(c);
    }
  }
  return os.str();
}

bool scope_contains(const std::string &scope, const std::string &token) {
  // OIDC scope is space-separated; do a token-boundary check rather than substring.
  std::size_t pos = 0;
  while (pos < scope.size()) {
    while (pos < scope.size() && scope[pos] == ' ') ++pos;
    std::size_t end = scope.find(' ', pos);
    std::string t = (end == std::string::npos)
                        ? scope.substr(pos)
                        : scope.substr(pos, end - pos);
    if (t == token) return true;
    if (end == std::string::npos) break;
    pos = end + 1;
  }
  return false;
}

} // namespace

sso::SsoHttpResult authorize(
    const std::map<std::string, std::string> &query,
    const std::string &cookie_header,
    IMongodbClient &db,
    IdpClientRegistry &registry,
    IdpSessionManager &sessions,
    sso::IClock &clock) {

  const std::string &response_type = get(query, "response_type");
  const std::string &client_id     = get(query, "client_id");
  const std::string &redirect_uri  = get(query, "redirect_uri");
  const std::string &scope         = get(query, "scope");
  const std::string &state         = get(query, "state");
  const std::string &nonce         = get(query, "nonce");
  const std::string &challenge     = get(query, "code_challenge");
  const std::string &chal_method   = get(query, "code_challenge_method");

  // ── parameter validation ───────────────────────────────────────────────
  // Order matters: validate client_id and redirect_uri FIRST so we can
  // be confident the RP we're about to talk to is one we know. Only
  // after that do we report errors that involve "the request" — those
  // could in principle be redirected to the RP per OIDC, but we follow
  // the safer "respond directly with 400" pattern for unknown clients.
  if (client_id.empty()) {
    return error_400("invalid_request", "missing client_id");
  }
  if (redirect_uri.empty()) {
    return error_400("invalid_request", "missing redirect_uri");
  }
  if (!registry.find(client_id)) {
    return error_400("invalid_client", "unknown client_id");
  }
  if (!registry.validate_redirect_uri(client_id, redirect_uri)) {
    return error_400("invalid_request",
                      "redirect_uri not registered for client");
  }

  if (response_type != "code") {
    return error_400("unsupported_response_type",
                      "only response_type=code is supported");
  }
  if (challenge.empty()) {
    return error_400("invalid_request", "code_challenge is required (PKCE)");
  }
  if (chal_method != "S256") {
    return error_400("invalid_request",
                      "code_challenge_method must be S256");
  }
  if (!scope_contains(scope, "openid")) {
    return error_400("invalid_scope", "scope must contain openid");
  }
  if (state.empty()) {
    return error_400("invalid_request",
                      "state is required (CSRF defense)");
  }

  const std::int64_t now = clock.now_unix();

  // ── session check ──────────────────────────────────────────────────────
  const std::string sid = parse_cookie(cookie_header, kIdpSessionCookieName);
  IdpAuthContext ctx = sid.empty() ? IdpAuthContext{} : sessions.lookup(sid);

  if (!ctx.valid) {
    // No session: persist the pending request, redirect to /login,
    // bind via the xpmile_idp_pending cookie.
    const std::string req_token = sso::random_token(32);
    nlohmann::json doc = {
        {"_id",                    req_token},
        {"client_id",              client_id},
        {"redirect_uri",           redirect_uri},
        {"scope",                  scope},
        {"state",                  state},
        {"nonce",                  nonce},
        {"code_challenge",         challenge},
        {"code_challenge_method",  chal_method},
        {"expiresAt",              now + kPendingMaxAgeSec},
    };
    db.create_document("idp", "idp_pending_auth", doc.dump());

    sso::SsoHttpResult r;
    r.status     = 302;
    r.location   = "/login";
    r.set_cookie = build_idp_pending_cookie(req_token);
    return r;
  }

  // ── session valid → mint code, redirect to RP ──────────────────────────
  const std::string code = sso::random_token(32);
  nlohmann::json code_doc = {
      {"_id",                   code},
      {"client_id",             client_id},
      {"user_sub",              ctx.accountCode},
      {"user_email",            ctx.email},
      {"user_name",             ctx.name},
      {"user_role",             ctx.role},
      {"redirect_uri",          redirect_uri},
      {"nonce",                 nonce},
      {"scope",                 scope},
      {"code_challenge",        challenge},
      {"code_challenge_method", chal_method},
      {"expiresAt",             now + kCodeMaxAgeSec},
  };
  db.create_document("idp", "idp_codes", code_doc.dump());

  std::ostringstream loc;
  loc << redirect_uri
      << (redirect_uri.find('?') == std::string::npos ? '?' : '&')
      << "code="   << url_encode(code)
      << "&state=" << url_encode(state);

  sso::SsoHttpResult r;
  r.status   = 302;
  r.location = loc.str();
  return r;
}

} // namespace idp
