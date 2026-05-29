#ifndef RUNCPULL_MANIFEST_HPP
#define RUNCPULL_MANIFEST_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

/**
 * @file manifest.hpp
 * @brief Parsers for the four manifest media types Docker Hub serves.
 *
 * Phase C of the runc-pull TDD plan. We accept:
 *
 *  - `application/vnd.oci.image.index.v1+json`
 *  - `application/vnd.docker.distribution.manifest.list.v2+json`
 *  - `application/vnd.oci.image.manifest.v1+json`
 *  - `application/vnd.docker.distribution.manifest.v2+json`
 *
 * The first two are "manifest lists" (one entry per platform). The last
 * two are "image manifests" (config blob + ordered layer blobs). Anything
 * else fails parsing — v1 of the puller is Docker Hub only.
 *
 * The shape difference between the OCI and Docker variants is in
 * `mediaType` strings and a handful of optional fields. Both shapes are
 * normalised to the structs below so downstream callers (the orchestrator)
 * don't branch on registry quirk.
 */
namespace runcpull {

// ── Manifest list (image index) entry ─────────────────────────────────────

/**
 * @brief One platform entry in an image index / manifest list.
 *
 * `digest` points at the per-platform image manifest; the orchestrator
 * issues a second `/manifests/<digest>` call to fetch it.
 */
struct PlatformEntry {
  std::string media_type;     ///< e.g. "application/vnd.oci.image.manifest.v1+json"
  std::string digest;         ///< "sha256:<hex>"
  std::int64_t size = 0;      ///< bytes, advisory
  std::string os;             ///< "linux", "windows", ...
  std::string architecture;   ///< "amd64", "arm64", "arm", ...
  std::string variant;        ///< "v7", "v8", "" — optional in OCI
};

/**
 * @brief Parsed image index / manifest list.
 *
 * `media_type` is the top-level `mediaType` from the document (OCI or
 * Docker shape). Entries appear in document order — `manifest_pick_platform`
 * uses that order as the stable tiebreak.
 */
struct ManifestList {
  std::string media_type;
  std::vector<PlatformEntry> entries;
};

// ── Single-arch image manifest ────────────────────────────────────────────

/**
 * @brief One layer entry in a single-arch image manifest.
 *
 * Layers are applied in array order (layer 0 is the base layer). The
 * unpacker walks the same order.
 */
struct LayerEntry {
  std::string media_type;     ///< gzip / zstd / uncompressed tar
  std::string digest;         ///< "sha256:<hex>"
  std::int64_t size = 0;      ///< bytes, advisory
};

/**
 * @brief Reference to the image config blob.
 *
 * The config blob is JSON — its parser is in `image_config.hpp`.
 */
struct ConfigDescriptor {
  std::string media_type;
  std::string digest;         ///< "sha256:<hex>" — required, format-validated
  std::int64_t size = 0;
};

/**
 * @brief Parsed single-arch image manifest (OCI v1 or Docker v2).
 */
struct ImageManifest {
  std::string media_type;
  ConfigDescriptor config;
  std::vector<LayerEntry> layers;
};

// ── Discriminated union over the two manifest shapes ──────────────────────

/**
 * @brief Sum of the parsed manifest variants.
 *
 * Exactly one of `list` / `image` is engaged after a successful parse.
 */
struct Manifest {
  enum class Kind { kList, kImage };
  Kind kind = Kind::kImage;
  std::optional<ManifestList>  list;
  std::optional<ImageManifest> image;

  bool is_list()  const { return kind == Kind::kList;  }
  bool is_image() const { return kind == Kind::kImage; }
};

/**
 * @brief Parse one of the four supported manifest media types from JSON.
 *
 * @param json_body raw response body from `GET /v2/<name>/manifests/<ref>`.
 * @return std::nullopt on malformed JSON, unsupported mediaType, or
 *         missing required fields (layers, config.digest format, ...).
 */
std::optional<Manifest> parse_manifest(const std::string &json_body);

// ── Platform selection ────────────────────────────────────────────────────

/**
 * @brief Pick a `PlatformEntry` from a list against the host target.
 *
 * Selection rules (in order):
 *  1. `os` must match exactly (case-insensitive).
 *  2. `architecture` must match exactly (case-insensitive).
 *  3. If `host_variant` is non-empty, prefer entries with `variant`
 *     equal to it.
 *  4. If `host_variant` is empty, prefer entries with an empty `variant`
 *     over entries with a defined `variant` (the host didn't specify, so
 *     "no variant" is the cleaner match — matches the OCI runtime-spec
 *     guidance).
 *  5. Stable tiebreak: earlier-in-document wins.
 *
 * @return index into @p list.entries on match, std::nullopt on no match.
 */
std::optional<std::size_t> manifest_pick_platform(
    const ManifestList &list,
    const std::string &host_os,
    const std::string &host_arch,
    const std::string &host_variant);

} // namespace runcpull

#endif // RUNCPULL_MANIFEST_HPP
