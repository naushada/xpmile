#include "pull_orchestrator.hpp"

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "bundle_writer.hpp"
#include "digest.hpp"
#include "image_config.hpp"
#include "layer_unpack.hpp"
#include "manifest.hpp"
#include "registry_client.hpp"

namespace fs = std::filesystem;

namespace runcpull {

namespace {

// Map a RegistryError into the corresponding PullError so the orchestrator
// stays in its own error vocabulary at the boundary.
PullError to_pull_error(RegistryError e) {
  switch (e) {
    case RegistryError::NONE:                 return PullError::NONE;
    case RegistryError::TRANSPORT:            return PullError::TRANSPORT;
    case RegistryError::AUTH:                 return PullError::AUTH;
    case RegistryError::MALFORMED_CHALLENGE:  return PullError::AUTH;
    case RegistryError::MALFORMED_TOKEN:      return PullError::AUTH;
    case RegistryError::MANIFEST_NOT_FOUND:   return PullError::MANIFEST_NOT_FOUND;
    case RegistryError::BLOB_NOT_FOUND:       return PullError::BLOB_NOT_FOUND;
    case RegistryError::REDIRECT_LIMIT:       return PullError::REDIRECT_LIMIT;
    case RegistryError::UNSUPPORTED_REGISTRY: return PullError::UNSUPPORTED_REGISTRY;
  }
  return PullError::TRANSPORT;
}

// Map an UnpackError to PullError. Path-traversal / symlink-escape /
// size-limit overflow / tar parse error all funnel into UNPACK_FAILED;
// the originating enum is surfaced as a numeric value in detail.
PullError to_pull_error_from_unpack(UnpackError) { return PullError::UNPACK_FAILED; }

// Stream a blob from the registry through both a SHA-256 digester and a
// byte sink. Returns the digester's hex once the stream completes.
struct BlobFetchResult {
  PullError    error = PullError::NONE;
  std::string  detail;
  std::string  body;          ///< full body (in-memory — fine for tests)
  std::string  digest_hex;    ///< SHA-256 of the full body, lower-hex
};
BlobFetchResult fetch_blob_with_digest(RegistryClient &reg,
                                          const std::string &digest_ref) {
  BlobFetchResult out;
  Digest digester;
  const RegistryError e = reg.fetch_blob(
      digest_ref,
      [&](const char *data, std::size_t n) {
        out.body.append(data, n);
        if (n) digester.update(data, n);
      });
  if (e != RegistryError::NONE) {
    out.error  = to_pull_error(e);
    out.detail = digest_ref;
    return out;
  }
  out.digest_hex = digester.finalize();
  return out;
}

// Resolve a manifest reference: if `ref.digest` is set, fetch that
// directly. Else fetch by tag (default "latest" — parse_ref already
// applied that default). Returns the parsed Manifest, or an error.
struct ResolvedManifest {
  PullError error = PullError::NONE;
  std::string detail;
  Manifest manifest;
  std::string media_type;
};
ResolvedManifest fetch_and_parse_manifest(RegistryClient &reg,
                                             const std::string &reference) {
  ResolvedManifest out;
  const ManifestFetch r = reg.fetch_manifest(reference);
  if (r.error != RegistryError::NONE) {
    out.error  = to_pull_error(r.error);
    out.detail = reference;
    return out;
  }
  auto parsed = parse_manifest(r.body);
  if (!parsed.has_value()) {
    out.error  = PullError::MANIFEST_PARSE;
    out.detail = "media-type=" + r.media_type;
    return out;
  }
  out.manifest   = std::move(*parsed);
  out.media_type = r.media_type;
  return out;
}

}  // namespace

// ── PullOrchestrator ─────────────────────────────────────────────────────

PullOrchestrator::PullOrchestrator(sso::IHttpClient &http, PullOptions opts)
    : m_http(http), m_opts(std::move(opts)) {}

PullResult PullOrchestrator::run(const ImageRef &ref,
                                  const std::string &bundle_dir) {
  PullResult res;
  const fs::path bundle = bundle_dir;

  // Refuse non-Hub registries early.
  if (ref.host != "docker.io") {
    res.error = PullError::UNSUPPORTED_REGISTRY;
    res.detail = ref.host;
    return res;
  }

  // RAII cleanup of `bundle` on failure paths — the orchestrator atomicity
  // contract is "no bundle dir on disk if we returned an error".
  bool keep_bundle = false;
  struct BundleCleanup {
    fs::path     dir;
    const bool  &keep;
    ~BundleCleanup() {
      if (!keep) {
        std::error_code ec;
        fs::remove_all(dir, ec);
      }
    }
  } cleanup{bundle, keep_bundle};

  RegistryClient reg(m_http, ref);

  // ── Step 1: fetch the manifest indicated by the ref ─────────────────────
  const std::string initial_ref =
      !ref.digest.empty() ? ref.digest : (ref.tag.empty() ? std::string("latest") : ref.tag);
  ResolvedManifest top = fetch_and_parse_manifest(reg, initial_ref);
  if (top.error != PullError::NONE) {
    res.error  = top.error;
    res.detail = top.detail;
    return res;
  }

  // ── Step 2: if it's an index/list AND the ref isn't digest-pinned,
  //           select the platform entry and fetch THAT manifest. The
  //           digest-pinned case skips the index step (HappyPath_F.1.4). ──
  ImageManifest image_mf;
  if (top.manifest.is_list() && ref.digest.empty()) {
    const ManifestList &lst = *top.manifest.list;
    const auto idx = manifest_pick_platform(
        lst, "linux", m_opts.target.name, m_opts.target.variant);
    if (!idx.has_value()) {
      res.error  = PullError::NO_MATCHING_PLATFORM;
      res.detail = std::string("linux/") + m_opts.target.name
                   + (m_opts.target.variant.empty()
                          ? ""
                          : "/" + m_opts.target.variant);
      return res;
    }
    const PlatformEntry &chosen = lst.entries[*idx];
    res.selected_platform = std::string("linux/") + chosen.architecture
                            + (chosen.variant.empty() ? "" : "/" + chosen.variant);

    ResolvedManifest perarch = fetch_and_parse_manifest(reg, chosen.digest);
    if (perarch.error != PullError::NONE) {
      res.error  = perarch.error;
      res.detail = perarch.detail;
      return res;
    }
    if (!perarch.manifest.is_image()) {
      res.error  = PullError::MANIFEST_PARSE;
      res.detail = "per-platform manifest at " + chosen.digest
                   + " is not a single-arch manifest";
      return res;
    }
    image_mf = std::move(*perarch.manifest.image);
  } else if (top.manifest.is_image()) {
    image_mf = std::move(*top.manifest.image);
    res.selected_platform = "(image)";
  } else {
    // Index/list with a digest-pinned ref — accept the OCI index but
    // signal to the user that they pinned an index, not a per-arch
    // manifest. This is an operator error.
    res.error  = PullError::MANIFEST_PARSE;
    res.detail = "digest-pinned reference resolves to an image index — "
                 "pin a per-arch digest instead";
    return res;
  }

  // ── Step 3: fetch + verify config blob ──────────────────────────────────
  auto config_digest_parsed = parse_digest_string(image_mf.config.digest);
  if (!config_digest_parsed.has_value()) {
    res.error  = PullError::MANIFEST_PARSE;
    res.detail = "bad config digest: " + image_mf.config.digest;
    return res;
  }
  BlobFetchResult config_blob =
      fetch_blob_with_digest(reg, image_mf.config.digest);
  if (config_blob.error != PullError::NONE) {
    res.error  = config_blob.error;
    res.detail = config_blob.detail;
    return res;
  }
  res.bytes_downloaded += config_blob.body.size();
  if (config_blob.digest_hex != config_digest_parsed->hex) {
    res.error  = PullError::CONFIG_DIGEST_MISMATCH;
    res.detail = image_mf.config.digest;
    return res;
  }
  auto image_cfg = parse_image_config(config_blob.body);
  if (!image_cfg.has_value()) {
    res.error = PullError::IMAGE_CONFIG_PARSE;
    return res;
  }

  // ── Step 4: write the bundle skeleton (creates rootfs/ + config.json
  //           atomically). On any later failure the BundleCleanup
  //           destructor removes `bundle`. ───────────────────────────────
  BundleWriterOptions bw_opts;
  bw_opts.force = m_opts.force_overwrite;
  try {
    BundleWriter().write(bundle, image_cfg->env, image_cfg->entrypoint,
                          image_cfg->cmd, bw_opts);
  } catch (const std::exception &ex) {
    res.error  = PullError::BUNDLE_WRITE_FAILED;
    res.detail = ex.what();
    return res;
  }

  // ── Step 5: for each layer, fetch + verify + unpack into rootfs/ ────────
  LayerUnpacker unpacker((bundle / "rootfs").string(), m_opts.unpack);
  for (const LayerEntry &layer : image_mf.layers) {
    auto layer_digest_parsed = parse_digest_string(layer.digest);
    if (!layer_digest_parsed.has_value()) {
      res.error  = PullError::MANIFEST_PARSE;
      res.detail = "bad layer digest: " + layer.digest;
      return res;
    }
    BlobFetchResult lb = fetch_blob_with_digest(reg, layer.digest);
    if (lb.error != PullError::NONE) {
      res.error  = lb.error;
      res.detail = lb.detail;
      return res;
    }
    res.bytes_downloaded += lb.body.size();
    if (lb.digest_hex != layer_digest_parsed->hex) {
      res.error  = PullError::LAYER_DIGEST_MISMATCH;
      res.detail = layer.digest;
      return res;
    }

    // Apply the layer. Gzip-detect by mediaType (Docker / OCI both use
    // "+gzip" in the layer media type for compressed layers).
    UnpackResult ur;
    if (layer.media_type.find("+gzip") != std::string::npos
        || layer.media_type.find("gzip") != std::string::npos) {
      ur = unpacker.apply_layer_tar_gz(lb.body);
    } else {
      ur = unpacker.apply_layer_tar(lb.body);
    }
    if (ur.error != UnpackError::NONE) {
      res.error  = to_pull_error_from_unpack(ur.error);
      res.detail = "layer=" + layer.digest + " unpack=" + ur.detail;
      return res;
    }
    ++res.layers_applied;
  }

  keep_bundle = true;
  return res;
}

}  // namespace runcpull
