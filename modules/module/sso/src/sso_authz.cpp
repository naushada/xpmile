#include "sso_authz.hpp"

namespace sso {

bool sso_requires_session(const std::string &uri) {
  if (uri.rfind("/api/v1/", 0) != 0) return false;      // not an API route
  if (uri == "/api/v1/account/login") return false;     // login itself
  if (uri.rfind("/api/v1/sso/", 0) == 0) return false;  // the SSO flow
  return true;
}

bool sso_authorize(const std::string &uri, bool has_valid_session) {
  return !sso_requires_session(uri) || has_valid_session;
}

} // namespace sso
