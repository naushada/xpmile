// runcpull::Digest — streaming SHA-256 + "alg:hex" parser. See digest.hpp.

#include "digest.hpp"

#include <algorithm>
#include <stdexcept>

namespace runcpull {

namespace {

// Lower-case hex of `len` bytes from `bytes`. The output is always
// 2 * len characters.
std::string to_hex(const unsigned char *bytes, std::size_t len) {
  static const char kHex[] = "0123456789abcdef";
  std::string out;
  out.resize(len * 2);
  for (std::size_t i = 0; i < len; ++i) {
    out[2 * i]     = kHex[(bytes[i] >> 4) & 0x0F];
    out[2 * i + 1] = kHex[bytes[i] & 0x0F];
  }
  return out;
}

bool is_hex_char(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
         (c >= 'A' && c <= 'F');
}

char to_lower_ascii(char c) {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

} // namespace

Digest::Digest() : ctx_(EVP_MD_CTX_new()), finalized_(false) {
  if (ctx_ == nullptr) {
    throw std::runtime_error("runcpull::Digest: EVP_MD_CTX_new failed");
  }
  if (EVP_DigestInit_ex(ctx_, EVP_sha256(), nullptr) != 1) {
    EVP_MD_CTX_free(ctx_);
    ctx_ = nullptr;
    throw std::runtime_error("runcpull::Digest: EVP_DigestInit_ex failed");
  }
}

Digest::~Digest() {
  if (ctx_ != nullptr) {
    EVP_MD_CTX_free(ctx_);
    ctx_ = nullptr;
  }
}

Digest::Digest(Digest &&other) noexcept
    : ctx_(other.ctx_), finalized_(other.finalized_) {
  other.ctx_ = nullptr;
  other.finalized_ = true;
}

Digest &Digest::operator=(Digest &&other) noexcept {
  if (this != &other) {
    if (ctx_ != nullptr) {
      EVP_MD_CTX_free(ctx_);
    }
    ctx_ = other.ctx_;
    finalized_ = other.finalized_;
    other.ctx_ = nullptr;
    other.finalized_ = true;
  }
  return *this;
}

void Digest::update(const void *data, std::size_t len) {
  if (finalized_) {
    throw std::runtime_error("runcpull::Digest::update after finalize");
  }
  if (len == 0) {
    return;
  }
  if (EVP_DigestUpdate(ctx_, data, len) != 1) {
    throw std::runtime_error("runcpull::Digest::update: EVP_DigestUpdate failed");
  }
}

std::string Digest::finalize() {
  if (finalized_) {
    throw std::runtime_error("runcpull::Digest::finalize called twice");
  }
  unsigned char md[EVP_MAX_MD_SIZE];
  unsigned int md_len = 0;
  if (EVP_DigestFinal_ex(ctx_, md, &md_len) != 1) {
    throw std::runtime_error("runcpull::Digest::finalize: EVP_DigestFinal_ex failed");
  }
  finalized_ = true;
  return to_hex(md, md_len);
}

bool Digest::verify(const std::string &expected_hex) {
  // Don't throw on bad input — callers want a bool. Length and char-set
  // checks first so a malformed expected value doesn't bypass the
  // constant-time compare.
  if (expected_hex.size() != 64) {
    return false;
  }
  for (char c : expected_hex) {
    if (!is_hex_char(c)) {
      return false;
    }
  }
  std::string got;
  try {
    got = finalize();
  } catch (...) {
    return false;
  }
  // Constant-time compare on lower-cased copies — OpenSSL's CRYPTO_memcmp
  // is the canonical primitive but we'd need to lowercase first; a
  // straightforward branch-free byte XOR achieves the same shape on a
  // tiny fixed 64-byte input.
  unsigned char diff = 0;
  for (std::size_t i = 0; i < 64; ++i) {
    diff |= static_cast<unsigned char>(
        to_lower_ascii(got[i]) ^ to_lower_ascii(expected_hex[i]));
  }
  return diff == 0;
}

std::optional<ParsedDigest> parse_digest_string(const std::string &s) {
  auto colon = s.find(':');
  if (colon == std::string::npos || colon == 0 || colon == s.size() - 1) {
    return std::nullopt;
  }
  // Reject multiple colons — `sha256:abc:def` is not a legal digest string.
  if (s.find(':', colon + 1) != std::string::npos) {
    return std::nullopt;
  }

  std::string alg = s.substr(0, colon);
  std::string hex = s.substr(colon + 1);

  std::transform(alg.begin(), alg.end(), alg.begin(), to_lower_ascii);
  std::transform(hex.begin(), hex.end(), hex.begin(), to_lower_ascii);

  if (alg != "sha256") {
    return std::nullopt;
  }
  if (hex.size() != 64) {
    return std::nullopt;
  }
  for (char c : hex) {
    if (!is_hex_char(c)) {
      return std::nullopt;
    }
  }

  return ParsedDigest{std::move(alg), std::move(hex)};
}

} // namespace runcpull
