#include "idp_cookie.hpp"

#include <sstream>

namespace idp {

namespace {

std::string build_named(const std::string &name, const std::string &value,
                         long max_age) {
  std::ostringstream os;
  os << name << "=" << value
     << "; HttpOnly; Secure; SameSite=Lax; Path=/api/v1/idp/"
     << "; Max-Age=" << max_age;
  return os.str();
}

} // namespace

std::string build_idp_session_cookie(const std::string &sid, long max_age) {
  return build_named(kIdpSessionCookieName, sid, max_age);
}

std::string build_idp_pending_cookie(const std::string &req_token,
                                      long max_age) {
  return build_named(kIdpPendingCookieName, req_token, max_age);
}

std::string build_expired_cookie(const std::string &name) {
  return build_named(name, "", 0);
}

std::string parse_cookie(const std::string &header, const std::string &name) {
  if (header.empty() || name.empty()) return {};
  const std::string key = name + "=";
  std::size_t pos = 0;
  while (pos < header.size()) {
    // Skip leading whitespace and ';' separators.
    while (pos < header.size() &&
           (header[pos] == ' ' || header[pos] == ';' || header[pos] == '\t')) {
      ++pos;
    }
    if (pos >= header.size()) break;
    std::size_t end = header.find(';', pos);
    std::string token = (end == std::string::npos)
                            ? header.substr(pos)
                            : header.substr(pos, end - pos);
    if (token.rfind(key, 0) == 0) {
      return token.substr(key.size());
    }
    if (end == std::string::npos) break;
    pos = end + 1;
  }
  return {};
}

} // namespace idp
