#ifndef RUNCPULL_REGISTRY_CLIENT_HPP
#define RUNCPULL_REGISTRY_CLIENT_HPP

/**
 * @file registry_client.hpp
 * @brief Docker Hub Registry v2 API client for the runc-pull tool.
 *
 * Phase B of the runc-pull TDD plan (docs/design/runc-pull/runc-pull-tdd-plan.md
 * §"Phase B — Docker Hub registry client").
 *
 * Pieces:
 *   - @ref runcpull::parse_ref           — image reference normalisation
 *   - @ref runcpull::parse_www_authenticate
 *                                          — Bearer challenge parsing
 *   - @ref runcpull::build_token_url     — token endpoint URL builder
 *   - @ref runcpull::parse_token_response
 *                                          — `/token` JSON parser
 *   - @ref runcpull::resolve_host_arch    — `uname -m` → IATA-ish arch enum
 *   - @ref runcpull::RegistryClient       — the per-image stateful client
 *
 * Reuse mandate: HTTP I/O is the extended @ref sso::IHttpClient from
 * Phase A (headers + streaming overloads). Redirect policy is
 * @ref sso::redirect_step. JSON parsing is `nlohmann::json` from
 * `modules/module/thirdparty/json.hpp`. No new dependencies.
 */

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "sso_http_client.hpp"

namespace runcpull {

// ── Image reference ──────────────────────────────────────────────────────

/// Parsed image reference. Either @c tag or @c digest is set; both stay
/// empty when neither is present (defaults to "latest" tag downstream).
struct ImageRef {
  std::string host;     ///< "docker.io" — only Hub is in scope for v1.
  std::string name;     ///< "naushada/xpmile-wsdbagent", "library/mongo"
  std::string tag;      ///< "latest", "4.4" — empty when digest pinned
  std::string digest;   ///< "sha256:abc…" — empty when tag-only
};

enum class RefError {
  NONE = 0,
  EMPTY,
  INVALID_CHARS,
  NON_HUB_REGISTRY,     ///< quay.io / ghcr.io etc. — v1 is Hub only
};

struct RefParseResult {
  ImageRef ref;
  RefError error = RefError::NONE;
};

/**
 * @brief Normalise a Docker Hub image reference string.
 *
 * Accepts these shapes:
 *   - `mongo:4.4`               → `docker.io/library/mongo:4.4`
 *   - `naushada/foo`             → `docker.io/naushada/foo:latest`
 *   - `naushada/foo:v1`          → `docker.io/naushada/foo:v1`
 *   - `naushada/foo@sha256:abc…` → digest-pinned, tag stays empty
 *   - `docker.io/...`            → preserved exactly
 *
 * Non-Hub hosts (quay.io / ghcr.io / etc.) return @c NON_HUB_REGISTRY —
 * Phase B's v1 scope is Hub only (the registry protocol generalises to
 * those hosts but each carries its own auth realm; deferred).
 *
 * When both `:tag` and `@digest` are present, the digest is preferred and
 * the tag is discarded — pinning is unambiguous.
 */
RefParseResult parse_ref(std::string_view input);

// ── Www-Authenticate Bearer challenge ────────────────────────────────────

struct AuthChallenge {
  std::string realm;     ///< token endpoint URL, e.g. https://auth.docker.io/token
  std::string service;   ///< service identifier, e.g. registry.docker.io
};

struct AuthChallengeParse {
  AuthChallenge value;
  bool          ok = false;
};

/**
 * @brief Parse a `Www-Authenticate: Bearer realm="…",service="…"` header.
 *
 * Accepts quoted and unquoted forms. Extracts @c realm and @c service; other
 * directives (e.g. @c scope) are ignored — the caller computes scope from
 * the image name.
 */
AuthChallengeParse parse_www_authenticate(std::string_view header_value);

// ── Token URL + token response ───────────────────────────────────────────

/**
 * @brief Build a Docker Hub token URL from a challenge.
 *
 * Returns
 * `https://auth.docker.io/token?service=registry.docker.io&scope=repository:<image>:<action>`
 * with @p image and @p action URL-encoded.
 */
std::string build_token_url(
    const std::string &realm,
    const std::string &service,
    const std::string &image_name,
    const std::string &action);

struct TokenParse {
  std::string token;
  bool        ok = false;
};

/**
 * @brief Extract the bearer token from a Docker Hub `/token` JSON body.
 *
 * Some registries return `{"token":"…"}`, some `{"access_token":"…"}` —
 * accept both. When both fields are present, prefer the canonical
 * `"token"`.
 */
TokenParse parse_token_response(std::string_view json_body);

// ── Host architecture ───────────────────────────────────────────────────

enum class HostArch {
  AMD64,
  ARM64,
  ARMV7,
  UNKNOWN,
};

/// Result of @ref resolve_host_arch including both the enum value and the
/// canonical name used in OCI manifest `platform.architecture` fields.
struct HostArchResult {
  HostArch    arch = HostArch::UNKNOWN;
  std::string name;            ///< "amd64", "arm64", "arm" — OCI canonical
  std::string variant;         ///< "v7", "v8" — OCI variant (often empty)
};

/// Test-injectable probe for `uname -m`.
class IHostProbe {
 public:
  virtual ~IHostProbe() = default;
  virtual std::string uname_m() const = 0;
};

/// Production probe — calls `uname(2)`.
class HostProbe : public IHostProbe {
 public:
  std::string uname_m() const override;
};

/// Map a `uname -m` result to a @ref HostArch.
HostArchResult resolve_host_arch(const IHostProbe &probe);

/// Apply a CLI `--arch` override. Recognises "amd64", "arm64", "armv7" /
/// "arm/v7" (variant form). Returns UNKNOWN on unrecognised input — caller
/// decides whether to error or fall back to probe.
HostArchResult resolve_host_arch_from_override(std::string_view flag);

// ── Registry client ─────────────────────────────────────────────────────

/// Error surface for @ref RegistryClient calls.
enum class RegistryError {
  NONE = 0,
  TRANSPORT,              ///< connection / TLS / read failure
  AUTH,                   ///< 401/403 from challenge or token endpoint
  MALFORMED_CHALLENGE,    ///< Www-Authenticate missing/invalid
  MALFORMED_TOKEN,        ///< token JSON missing both fields
  MANIFEST_NOT_FOUND,     ///< 404 from /manifests/<ref>
  BLOB_NOT_FOUND,         ///< 404 from /blobs/<digest>
  REDIRECT_LIMIT,         ///< too many CDN hops
  UNSUPPORTED_REGISTRY,   ///< non-Hub host (out of v1 scope)
};

struct ManifestFetch {
  std::string    body;
  std::string    media_type;        ///< Content-Type without parameters
  RegistryError  error = RegistryError::NONE;
  std::string    detail;
};

/**
 * @brief Drives the per-image probe → token → manifest / blob sequence
 *        against Docker Hub.
 *
 * One @ref RegistryClient instance is meaningful for one @ref ImageRef
 * at a time — the bearer token it acquires is image-scoped. Reuse across
 * pulls is OK so long as the underlying @ref sso::IHttpClient is
 * thread-safe (the production one is not; orchestrator pulls one image
 * at a time today).
 */
class RegistryClient {
 public:
  /// @param http  Outbound HTTP client (Phase A extended `IHttpClient`).
  ///              Must outlive this object.
  /// @param image The reference being pulled (sets the bearer scope).
  RegistryClient(sso::IHttpClient &http, ImageRef image);

  /**
   * @brief Fetch a manifest. The probe-then-token sequence is cached so
   *        subsequent calls for the same image reuse the bearer.
   *
   * @p reference  is either the tag (e.g. `"latest"`, `"4.4"`) or a digest
   *               like `"sha256:abc…"`. The `<reference>` URL component
   *               in `/v2/<name>/manifests/<reference>`.
   *
   * The `Accept` header sent is the OR of all four supported media types:
   * OCI image index, Docker manifest list, OCI image manifest, Docker
   * manifest v2 — see the design doc §5.
   */
  ManifestFetch fetch_manifest(const std::string &reference);

  /**
   * @brief Stream a blob (image config or layer) through @p on_chunk.
   *
   * Handles the 307 Hub→CDN redirect by dropping `Authorization` on the
   * cross-origin hop (curl / browser policy — and the CDN doesn't want
   * the registry bearer anyway). On failure, the callback may have
   * received a partial body; callers compute their digest streaming
   * alongside this and verify after `error == NONE`.
   */
  RegistryError fetch_blob(
      const std::string &digest,
      const sso::BodyChunkCallback &on_chunk);

  /// Inspect the cached token (for tests).
  const std::string &cached_token() const { return m_token; }
  std::size_t       token_fetch_count() const { return m_token_fetches; }

 private:
  // probe → /token → cache. Returns NONE on success or an auth error.
  RegistryError ensure_token();

  sso::IHttpClient &m_http;
  ImageRef          m_image;
  std::string       m_token;
  std::size_t       m_token_fetches = 0;
};

}  // namespace runcpull

#endif  // RUNCPULL_REGISTRY_CLIENT_HPP
