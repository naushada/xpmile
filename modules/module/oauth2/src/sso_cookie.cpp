#include "sso_cookie.hpp"

#include <cctype>

namespace sso {

namespace {

// Standard attribute suffix shared by every session cookie this module emits.
constexpr const char *kCookieAttributes =
    "; HttpOnly; Secure; SameSite=Lax; Path=/";

std::string trim(const std::string &s) {
  std::size_t b = 0, e = s.size();
  while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
  return s.substr(b, e - b);
}

} // namespace

std::string build_session_cookie(const std::string &sid, long max_age) {
  return std::string(kSessionCookieName) + "=" + sid + kCookieAttributes +
         "; Max-Age=" + std::to_string(max_age);
}

std::string build_expired_cookie() {
  return std::string(kSessionCookieName) + "=" + kCookieAttributes +
         "; Max-Age=0";
}

std::string parse_session_cookie(const std::string &cookie_header) {
  std::size_t pos = 0;
  while (pos <= cookie_header.size()) {
    std::size_t semi = cookie_header.find(';', pos);
    std::size_t len =
        (semi == std::string::npos) ? std::string::npos : semi - pos;
    std::string seg = trim(cookie_header.substr(pos, len));

    std::size_t eq = seg.find('=');
    if (eq != std::string::npos &&
        trim(seg.substr(0, eq)) == kSessionCookieName) {
      return trim(seg.substr(eq + 1));
    }

    if (semi == std::string::npos) break;
    pos = semi + 1;
  }
  return {};
}

} // namespace sso
