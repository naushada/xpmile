#include "sso_util.hpp"

#include <cstdint>
#include <vector>

#include <openssl/rand.h>

namespace sso {

namespace {

// Map a base64url character to its 6-bit value, or -1 if not in the alphabet.
int b64url_sextet(unsigned char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '-') return 62;
  if (c == '_') return 63;
  return -1;
}

} // namespace

std::string base64url_encode(const unsigned char *data, std::size_t len) {
  static const char tbl[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::string out;
  out.reserve((len + 2) / 3 * 4);
  std::size_t i = 0;
  for (; i + 2 < len; i += 3) {
    std::uint32_t n = (static_cast<std::uint32_t>(data[i]) << 16) |
                      (static_cast<std::uint32_t>(data[i + 1]) << 8) |
                      static_cast<std::uint32_t>(data[i + 2]);
    out += tbl[(n >> 18) & 0x3F];
    out += tbl[(n >> 12) & 0x3F];
    out += tbl[(n >> 6) & 0x3F];
    out += tbl[n & 0x3F];
  }
  if (i < len) {
    std::uint32_t n = static_cast<std::uint32_t>(data[i]) << 16;
    if (i + 1 < len) n |= static_cast<std::uint32_t>(data[i + 1]) << 8;
    out += tbl[(n >> 18) & 0x3F];
    out += tbl[(n >> 12) & 0x3F];
    if (i + 1 < len) out += tbl[(n >> 6) & 0x3F];
  }
  return out;
}

std::string base64url_decode(const std::string &in) {
  std::string out;
  std::uint32_t buf = 0;
  int bits = 0;
  for (unsigned char c : in) {
    if (c == '=') break;  // tolerate (but do not require) padding
    const int v = b64url_sextet(c);
    if (v < 0) return {};  // character outside the base64url alphabet
    buf = (buf << 6) | static_cast<std::uint32_t>(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out += static_cast<char>((buf >> bits) & 0xFF);
    }
  }
  return out;
}

std::string base64_encode(const std::string &in) {
  static const char tbl[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  const auto *data = reinterpret_cast<const unsigned char *>(in.data());
  const std::size_t len = in.size();
  std::string out;
  out.reserve((len + 2) / 3 * 4);
  std::size_t i = 0;
  for (; i + 2 < len; i += 3) {
    std::uint32_t n = (static_cast<std::uint32_t>(data[i]) << 16) |
                      (static_cast<std::uint32_t>(data[i + 1]) << 8) |
                      static_cast<std::uint32_t>(data[i + 2]);
    out += tbl[(n >> 18) & 0x3F];
    out += tbl[(n >> 12) & 0x3F];
    out += tbl[(n >> 6) & 0x3F];
    out += tbl[n & 0x3F];
  }
  if (i < len) {
    const bool two = (i + 1 < len);
    std::uint32_t n = static_cast<std::uint32_t>(data[i]) << 16;
    if (two) n |= static_cast<std::uint32_t>(data[i + 1]) << 8;
    out += tbl[(n >> 18) & 0x3F];
    out += tbl[(n >> 12) & 0x3F];
    out += two ? tbl[(n >> 6) & 0x3F] : '=';
    out += '=';
  }
  return out;
}

std::string base64_decode(const std::string &in) {
  auto sextet = [](unsigned char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  };
  std::string out;
  std::uint32_t buf = 0;
  int bits = 0;
  for (unsigned char c : in) {
    if (c == '=') break;
    if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
    const int v = sextet(c);
    if (v < 0) return {};  // character outside the base64 alphabet
    buf = (buf << 6) | static_cast<std::uint32_t>(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out += static_cast<char>((buf >> bits) & 0xFF);
    }
  }
  return out;
}

std::string random_token(std::size_t nbytes) {
  if (nbytes == 0) return {};
  std::vector<unsigned char> buf(nbytes);
  if (RAND_bytes(buf.data(), static_cast<int>(nbytes)) != 1) return {};
  return base64url_encode(buf.data(), nbytes);
}

} // namespace sso
