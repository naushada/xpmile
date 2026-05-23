#include "sso_csrf.hpp"

namespace sso {

namespace {

bool is_safe_method(const std::string &method) {
  return method == "GET" || method == "HEAD" || method == "OPTIONS";
}

// Extract a named cookie's value from a "k=v; k=v" Cookie header.
std::string cookie_value(const std::string &header, const std::string &name) {
  std::size_t pos = 0;
  while (pos < header.size()) {
    while (pos < header.size() && (header[pos] == ' ' || header[pos] == ';'))
      ++pos;
    const std::size_t eq = header.find('=', pos);
    if (eq == std::string::npos) break;
    const std::string key = header.substr(pos, eq - pos);
    std::size_t end = header.find(';', eq);
    if (end == std::string::npos) end = header.size();
    if (key == name) return header.substr(eq + 1, end - eq - 1);
    pos = end + 1;
  }
  return {};
}

} // namespace

std::string build_csrf_cookie(const std::string &token) {
  return std::string(kCsrfCookieName) + "=" + token +
         "; Secure; SameSite=Lax; Path=/";
}

std::string parse_csrf_cookie(const std::string &cookie_header) {
  return cookie_value(cookie_header, kCsrfCookieName);
}

bool csrf_ok(const std::string &method, const std::string &cookie_header,
             const std::string &header_token) {
  if (is_safe_method(method)) return true;
  const std::string cookie_token = parse_csrf_cookie(cookie_header);
  return !cookie_token.empty() && cookie_token == header_token;
}

} // namespace sso
