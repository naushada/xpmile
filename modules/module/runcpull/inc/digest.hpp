#ifndef RUNCPULL_DIGEST_HPP
#define RUNCPULL_DIGEST_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include <openssl/evp.h>

/**
 * @file digest.hpp
 * @brief SHA-256 streaming digest + "alg:hex" digest-string parser.
 *
 * Phase C of the runc-pull TDD plan. The puller verifies SHA-256 of every
 * blob it pulls (image manifest, image config, every layer). This wrapper
 * sits on top of OpenSSL's EVP_Digest* — the same primitives the IdP /
 * SSO modules already link.
 *
 * Only SHA-256 is supported in v1, matching the digest spec used by the
 * Docker Hub registry v2 API (`sha256:<hex>`). Other algorithms (sha512,
 * blake3, ...) are rejected by `parse_digest_string` so a malicious
 * manifest cannot downgrade the verifier silently.
 */
namespace runcpull {

/// Hex SHA-256 of the empty string — useful canonical value, used by tests
/// to assert the digester wires `EVP_DigestInit/Update/Final` correctly.
inline constexpr const char *kEmptySha256Hex =
    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

/**
 * @brief Streaming SHA-256 digest.
 *
 * Owns an `EVP_MD_CTX` for the lifetime of the object. Construct, call
 * `update()` zero or more times as bytes arrive, then exactly one of
 * `finalize()` (get the hex digest) or `verify()` (compare against a
 * known hex string in constant time).
 *
 * Not thread-safe. Not copyable. Move-only.
 */
class Digest {
public:
  /// Initialises a fresh SHA-256 context. Throws `std::runtime_error` if
  /// OpenSSL refuses to allocate / init.
  Digest();
  ~Digest();

  Digest(const Digest &) = delete;
  Digest &operator=(const Digest &) = delete;
  Digest(Digest &&other) noexcept;
  Digest &operator=(Digest &&other) noexcept;

  /// Feed @p len bytes from @p data into the running digest. No-op when
  /// @p len == 0. Calling `update` after `finalize/verify` is a programmer
  /// error and will throw.
  void update(const void *data, std::size_t len);

  /// Variant for convenience — `update(std::string_view)` shape.
  void update(const std::string &s) { update(s.data(), s.size()); }

  /// Compute the digest and return it as a 64-character lower-hex string.
  /// Sentinel: may only be called once per Digest instance.
  std::string finalize();

  /// Convenience: `finalize() == expected_hex` (case-insensitive on the
  /// hex). Returns false on length mismatch or non-hex characters without
  /// throwing — callers usually want a bool, not an exception.
  bool verify(const std::string &expected_hex);

private:
  EVP_MD_CTX *ctx_;
  bool finalized_;
};

/**
 * @brief Result of `parse_digest_string`.
 *
 * `alg` is always lower-case; `hex` is always lower-case 64-char hex.
 */
struct ParsedDigest {
  std::string alg;
  std::string hex;
};

/**
 * @brief Parse a registry-style digest reference (`sha256:<hex>`).
 *
 * Returns std::nullopt when:
 *  - the input lacks a single `:` separator
 *  - the algorithm is not `sha256` (no other alg supported in v1)
 *  - the hex portion is not exactly 64 lower-hex characters
 *
 * Upper-case hex is accepted and lower-cased in the output; the canonical
 * form a registry serves is lower-case but operator input may be mixed.
 */
std::optional<ParsedDigest> parse_digest_string(const std::string &s);

} // namespace runcpull

#endif // RUNCPULL_DIGEST_HPP
