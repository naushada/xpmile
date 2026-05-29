#ifndef RUNCPULL_PULL_ORCHESTRATOR_HPP
#define RUNCPULL_PULL_ORCHESTRATOR_HPP

/**
 * @file pull_orchestrator.hpp
 * @brief End-to-end driver for a single image pull.
 *
 * Phase F of the runc-pull TDD plan (docs/design/runc-pull/runc-pull-tdd-plan.md
 * §"Phase F"). Wires together:
 *
 *   - Phase B's @ref runcpull::RegistryClient for HTTP I/O against the
 *     Docker Hub v2 API (probe → token → manifest → blobs).
 *   - Phase C's @ref runcpull::parse_manifest / @ref runcpull::parse_image_config
 *     / @ref runcpull::Digest for JSON parsing + per-blob SHA-256
 *     verification.
 *   - Phase D's @ref runcpull::LayerUnpacker to apply layer streams to
 *     the bundle's rootfs.
 *   - Phase E's @ref runcpull::BundleWriter to materialise the bundle dir
 *     atomically.
 *
 * Atomicity contract: on any failure mid-pull the orchestrator removes
 * the bundle directory it (or BundleWriter) created — there is no partial
 * bundle on disk after a failed call. Tests in the F.2 set exercise this
 * across digest-mismatch, layer-HTTP-500, unpack-path-traversal, etc.
 *
 * Reuse mandate: all dependencies are existing phases (A through E) +
 * the `nlohmann::json` already vendored in `modules/module/thirdparty/`.
 * No new dependencies.
 */

#include <cstddef>
#include <cstdint>
#include <string>

#include "layer_unpack.hpp"
#include "registry_client.hpp"
#include "sso_http_client.hpp"

namespace runcpull {

/// Options that tune a single pull. Constructed once per @ref PullOrchestrator.
struct PullOptions {
  /// Target architecture for platform selection. Filled in from
  /// `resolve_host_arch` / `resolve_host_arch_from_override` by the CLI.
  HostArchResult target;

  /// Forwarded to the @ref LayerUnpacker — strip-suid, size caps, etc.
  /// Default values are the safe production defaults.
  UnpackOptions unpack;

  /// `true` lets the writer overwrite an existing bundle directory. CLI
  /// `--force`. Default false matches the BundleWriter / installer rule
  /// that re-pulls without `--force` are user errors.
  bool force_overwrite = false;
};

/// Error surface for @ref PullOrchestrator::run. Each variant maps to a
/// distinct CLI exit code in Phase G.
enum class PullError {
  NONE = 0,

  /// Networking / TLS / read failure. RegistryClient::TRANSPORT, etc.
  TRANSPORT,

  /// Token endpoint refused us or registry challenge was malformed.
  AUTH,

  /// `/manifests/<ref>` returned 404.
  MANIFEST_NOT_FOUND,

  /// Manifest JSON was parseable but the `mediaType` is one we don't
  /// support (neither image index nor single-arch manifest, in either
  /// the OCI or Docker shape).
  MANIFEST_PARSE,

  /// Image config JSON failed to parse.
  IMAGE_CONFIG_PARSE,

  /// The image index has no entry that matches the host (or override)
  /// architecture / OS. Operator sees `linux/<arch>` in @ref PullResult
  /// detail.
  NO_MATCHING_PLATFORM,

  /// SHA-256 of the downloaded config blob doesn't match the manifest's
  /// recorded digest. Highest-trust failure — distinguished from
  /// `TRANSPORT` so the CLI exit code 4 is unambiguous.
  CONFIG_DIGEST_MISMATCH,

  /// SHA-256 of a downloaded layer blob doesn't match its digest.
  LAYER_DIGEST_MISMATCH,

  /// A blob 404'd.
  BLOB_NOT_FOUND,

  /// `LayerUnpacker` failure — out-of-rootfs symlink, path traversal,
  /// size-limit overflow, tar/gzip parse error, etc. The originating
  /// `UnpackError` is mirrored in @ref PullResult::detail.
  UNPACK_FAILED,

  /// `BundleWriter::write` threw / refused (e.g. existing bundle dir
  /// without `force_overwrite`).
  BUNDLE_WRITE_FAILED,

  /// Registry redirect chain hit the hop limit.
  REDIRECT_LIMIT,

  /// Reference points at a non-Hub host. Out of v1 scope.
  UNSUPPORTED_REGISTRY,
};

/// Result of @ref PullOrchestrator::run.
struct PullResult {
  PullError    error = PullError::NONE;
  std::string  detail;             ///< e.g. offending digest, layer name
  std::size_t  layers_applied = 0;
  std::uint64_t bytes_downloaded = 0;
  std::string  selected_platform;  ///< "linux/arm64", "linux/arm/v7" etc.
};

/**
 * @brief Drive a single image pull end-to-end into @p bundle_dir.
 *
 * Construction is cheap; one @ref RegistryClient is created internally
 * per pull (the bearer is image-scoped). Reuse across pulls of different
 * images is fine but reuse for the same image is wasteful — every `run`
 * issues a fresh probe → token sequence (#F.1.5 verifies the token is
 * NOT refreshed across blob fetches inside a single run; the test cares
 * about that boundary, not the cross-run boundary).
 *
 * For digest-pinned refs (`…@sha256:…`), the orchestrator skips the
 * platform-selection step and treats the digest as the canonical
 * single-arch manifest — `HappyPath_DigestRefSkipsIndexStep` in F.1.
 */
class PullOrchestrator {
 public:
  /// @param http  Outbound HTTP client (Phase A extended `IHttpClient`).
  ///              Must outlive the orchestrator.
  PullOrchestrator(sso::IHttpClient &http, PullOptions opts);

  /**
   * @brief Pull @p ref into @p bundle_dir.
   *
   * On success the bundle is at @p bundle_dir with a populated `rootfs/`
   * and a stub `config.json` the operator overwrites with the per-service
   * template from `docs/operator-runc.md §5`.
   *
   * On any failure the directory is removed before returning — the F.2
   * tests confirm no partial bundle is left on disk.
   */
  PullResult run(const ImageRef &ref, const std::string &bundle_dir);

 private:
  sso::IHttpClient &m_http;
  PullOptions       m_opts;
};

}  // namespace runcpull

#endif  // RUNCPULL_PULL_ORCHESTRATOR_HPP
