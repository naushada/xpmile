// runcpull manifest parsers — image index / manifest list / image manifest.
// See manifest.hpp.

#include "manifest.hpp"

#include "digest.hpp"
#include "json.hpp"

#include <algorithm>
#include <cctype>

namespace runcpull {

namespace {

constexpr const char *kOciImageIndexMediaType =
    "application/vnd.oci.image.index.v1+json";
constexpr const char *kDockerManifestListMediaType =
    "application/vnd.docker.distribution.manifest.list.v2+json";
constexpr const char *kOciImageManifestMediaType =
    "application/vnd.oci.image.manifest.v1+json";
constexpr const char *kDockerManifestV2MediaType =
    "application/vnd.docker.distribution.manifest.v2+json";

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return s;
}

bool is_list_media_type(const std::string &mt) {
  return mt == kOciImageIndexMediaType || mt == kDockerManifestListMediaType;
}

bool is_image_media_type(const std::string &mt) {
  return mt == kOciImageManifestMediaType || mt == kDockerManifestV2MediaType;
}

// Read a JSON field as a string with a default. Tolerates missing fields
// — the caller decides whether absence is fatal.
std::string get_str(const nlohmann::json &j, const char *key,
                    const std::string &dflt = "") {
  auto it = j.find(key);
  if (it == j.end() || !it->is_string()) return dflt;
  return it->get<std::string>();
}

std::int64_t get_i64(const nlohmann::json &j, const char *key,
                     std::int64_t dflt = 0) {
  auto it = j.find(key);
  if (it == j.end() || !it->is_number_integer()) return dflt;
  return it->get<std::int64_t>();
}

// Validate "sha256:<64-hex>" using the same parser the digest module
// exposes — single source of truth, no copy/paste of the regex.
bool is_valid_sha256_digest_string(const std::string &s) {
  return parse_digest_string(s).has_value();
}

std::optional<ManifestList> parse_list(const nlohmann::json &doc,
                                       const std::string &media_type) {
  ManifestList out;
  out.media_type = media_type;

  auto it = doc.find("manifests");
  if (it == doc.end() || !it->is_array()) {
    return std::nullopt;
  }

  for (const auto &entry : *it) {
    if (!entry.is_object()) return std::nullopt;
    PlatformEntry pe;
    pe.media_type = get_str(entry, "mediaType");
    pe.digest = get_str(entry, "digest");
    pe.size = get_i64(entry, "size");
    auto pit = entry.find("platform");
    if (pit != entry.end() && pit->is_object()) {
      pe.os = get_str(*pit, "os");
      pe.architecture = get_str(*pit, "architecture");
      pe.variant = get_str(*pit, "variant");
    }
    if (pe.digest.empty() || !is_valid_sha256_digest_string(pe.digest)) {
      return std::nullopt;
    }
    out.entries.push_back(std::move(pe));
  }
  return out;
}

std::optional<ImageManifest> parse_image(const nlohmann::json &doc,
                                         const std::string &media_type) {
  ImageManifest out;
  out.media_type = media_type;

  // config { mediaType, digest, size } — required.
  auto cit = doc.find("config");
  if (cit == doc.end() || !cit->is_object()) return std::nullopt;
  out.config.media_type = get_str(*cit, "mediaType");
  out.config.digest = get_str(*cit, "digest");
  out.config.size = get_i64(*cit, "size");
  if (!is_valid_sha256_digest_string(out.config.digest)) {
    return std::nullopt;
  }

  // layers[] — required, must be a non-empty array of objects with
  // a valid sha256 digest each.
  auto lit = doc.find("layers");
  if (lit == doc.end() || !lit->is_array() || lit->empty()) {
    return std::nullopt;
  }
  for (const auto &layer : *lit) {
    if (!layer.is_object()) return std::nullopt;
    LayerEntry le;
    le.media_type = get_str(layer, "mediaType");
    le.digest = get_str(layer, "digest");
    le.size = get_i64(layer, "size");
    if (!is_valid_sha256_digest_string(le.digest)) {
      return std::nullopt;
    }
    out.layers.push_back(std::move(le));
  }
  return out;
}

} // namespace

std::optional<Manifest> parse_manifest(const std::string &json_body) {
  nlohmann::json doc;
  try {
    doc = nlohmann::json::parse(json_body);
  } catch (const nlohmann::json::exception &) {
    return std::nullopt;
  }
  if (!doc.is_object()) return std::nullopt;

  std::string mt = get_str(doc, "mediaType");
  if (mt.empty()) return std::nullopt;

  if (is_list_media_type(mt)) {
    auto list = parse_list(doc, mt);
    if (!list) return std::nullopt;
    Manifest m;
    m.kind = Manifest::Kind::kList;
    m.list = std::move(*list);
    return m;
  }
  if (is_image_media_type(mt)) {
    auto img = parse_image(doc, mt);
    if (!img) return std::nullopt;
    Manifest m;
    m.kind = Manifest::Kind::kImage;
    m.image = std::move(*img);
    return m;
  }
  return std::nullopt; // unsupported mediaType
}

std::optional<std::size_t> manifest_pick_platform(
    const ManifestList &list,
    const std::string &host_os,
    const std::string &host_arch,
    const std::string &host_variant) {
  const std::string host_os_lc = lower(host_os);
  const std::string host_arch_lc = lower(host_arch);
  const std::string host_var_lc = lower(host_variant);

  std::optional<std::size_t> best;
  // Score: higher is better. 2 = exact variant match (or both empty when
  // requested), 1 = no variant requested and entry has none, 0 = arch+os
  // match but variant differs. We *don't* accept entries that have a
  // defined variant when the host requested one that doesn't match —
  // those score below the "no variant" entries.
  int best_score = -1;
  for (std::size_t i = 0; i < list.entries.size(); ++i) {
    const auto &e = list.entries[i];
    if (lower(e.os) != host_os_lc) continue;
    if (lower(e.architecture) != host_arch_lc) continue;

    const std::string entry_var_lc = lower(e.variant);
    int score;
    if (!host_var_lc.empty()) {
      // Host has a variant — exact match wins, else fall back to entries
      // with no variant (a typical OCI index has variant set on
      // every arm entry, so this branch may never fire, but the rule is
      // consistent with the no-variant case below).
      if (entry_var_lc == host_var_lc) {
        score = 2;
      } else if (entry_var_lc.empty()) {
        score = 1;
      } else {
        continue; // variant explicitly mismatches → not a candidate
      }
    } else {
      // No host variant requested — prefer no-variant entries, then fall
      // back to any defined variant (so an arm64-v8-only index still
      // matches an arm64 host).
      if (entry_var_lc.empty()) {
        score = 2;
      } else {
        score = 1;
      }
    }

    if (score > best_score) {
      best_score = score;
      best = i;
    }
  }
  return best;
}

} // namespace runcpull
