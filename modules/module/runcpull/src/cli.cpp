#include "cli.hpp"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>

namespace runcpull {

// ── parse_size_with_units ────────────────────────────────────────────────

std::uint64_t parse_size_with_units(const std::string &s) {
  if (s.empty()) return 0;
  std::size_t i = 0;
  std::uint64_t n = 0;
  while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
    n = n * 10 + static_cast<std::uint64_t>(s[i] - '0');
    ++i;
  }
  if (i == 0) return 0;
  std::uint64_t mult = 1;
  if (i < s.size()) {
    const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
    switch (c) {
      case 'k': mult = 1024ULL; break;
      case 'm': mult = 1024ULL * 1024ULL; break;
      case 'g': mult = 1024ULL * 1024ULL * 1024ULL; break;
      case 't': mult = 1024ULL * 1024ULL * 1024ULL * 1024ULL; break;
      case 'b': mult = 1ULL; break;
      default:  return 0;   // unknown suffix
    }
    ++i;
    // Allow trailing 'B' (kB, MB, etc.).
    if (i < s.size() && (s[i] == 'B' || s[i] == 'b')) ++i;
  }
  if (i != s.size()) return 0;
  return n * mult;
}

// ── parse_args ───────────────────────────────────────────────────────────

namespace {
bool need_value(const char *flag, int i, int argc, char *const argv[],
                  CliParseResult &out) {
  if (i + 1 >= argc) {
    out.error  = CliParseError::BAD_VALUE;
    out.detail = std::string(flag) + " requires a value";
    return false;
  }
  return true;
}
}  // namespace

CliParseResult parse_args(int argc, char *const argv[]) {
  CliParseResult out;

  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "-h" || a == "--help") {
      out.args.help = true;
    } else if (a == "-V" || a == "--version") {
      out.args.version = true;
    } else if (a == "--force") {
      out.args.force = true;
    } else if (a == "--to") {
      if (!need_value("--to", i, argc, argv, out)) return out;
      out.args.to_dir = argv[++i];
    } else if (a == "--arch") {
      if (!need_value("--arch", i, argc, argv, out)) return out;
      out.args.arch = resolve_host_arch_from_override(argv[++i]);
    } else if (a == "--max-entry-size") {
      if (!need_value("--max-entry-size", i, argc, argv, out)) return out;
      out.args.max_entry_size = parse_size_with_units(argv[++i]);
      if (out.args.max_entry_size == 0) {
        out.error  = CliParseError::BAD_VALUE;
        out.detail = std::string("--max-entry-size ") + argv[i];
        return out;
      }
    } else if (a == "--max-total-size") {
      if (!need_value("--max-total-size", i, argc, argv, out)) return out;
      out.args.max_total_size = parse_size_with_units(argv[++i]);
      if (out.args.max_total_size == 0) {
        out.error  = CliParseError::BAD_VALUE;
        out.detail = std::string("--max-total-size ") + argv[i];
        return out;
      }
    } else if (!a.empty() && a[0] == '-') {
      out.error  = CliParseError::UNKNOWN_FLAG;
      out.detail = a;
      return out;
    } else {
      // Positional: first hit is the image.
      if (out.args.image.empty()) out.args.image = a;
      else {
        out.error  = CliParseError::UNKNOWN_FLAG;
        out.detail = a;
        return out;
      }
    }
  }

  // help / version short-circuit further validation.
  if (out.args.help || out.args.version) return out;

  if (out.args.image.empty()) {
    out.error  = CliParseError::MISSING_IMAGE;
    out.detail = "missing image reference";
    return out;
  }
  if (out.args.to_dir.empty()) {
    out.error  = CliParseError::MISSING_TO;
    out.detail = "missing --to <bundle-dir>";
    return out;
  }
  return out;
}

// ── CliArgs::to_pull_options ─────────────────────────────────────────────

PullOptions CliArgs::to_pull_options(const HostArchResult &fallback) const {
  PullOptions o;
  o.target          = (arch.arch == HostArch::UNKNOWN && arch.name.empty())
                          ? fallback : arch;
  o.force_overwrite = force;
  if (max_entry_size) o.unpack.max_entry_size = max_entry_size;
  if (max_total_size) o.unpack.max_total_size = max_total_size;
  return o;
}

// ── pull_error_to_exit_code ──────────────────────────────────────────────

int pull_error_to_exit_code(PullError e) {
  switch (e) {
    case PullError::NONE:                    return 0;
    case PullError::TRANSPORT:                return 1;
    case PullError::AUTH:                     return 2;
    case PullError::MANIFEST_NOT_FOUND:       return 3;
    case PullError::MANIFEST_PARSE:           return 3;
    case PullError::NO_MATCHING_PLATFORM:     return 3;
    case PullError::IMAGE_CONFIG_PARSE:       return 3;
    case PullError::UNSUPPORTED_REGISTRY:     return 3;
    case PullError::CONFIG_DIGEST_MISMATCH:   return 4;
    case PullError::LAYER_DIGEST_MISMATCH:    return 4;
    case PullError::UNPACK_FAILED:            return 5;
    case PullError::BUNDLE_WRITE_FAILED:      return 5;
    case PullError::BLOB_NOT_FOUND:           return 1;
    case PullError::REDIRECT_LIMIT:           return 1;
  }
  return 1;
}

int cli_parse_error_to_exit_code(CliParseError e) {
  return (e == CliParseError::NONE) ? 0 : 6;
}

// ── format_pull_summary ──────────────────────────────────────────────────

namespace {
std::string format_size(std::uint64_t b) {
  std::ostringstream os;
  if      (b >= 1024ULL * 1024 * 1024) os << (b / (1024ULL * 1024 * 1024)) << " GB";
  else if (b >= 1024ULL * 1024)         os << (b / (1024ULL * 1024))        << " MB";
  else if (b >= 1024ULL)                 os << (b / 1024ULL)                  << " KB";
  else                                    os << b << " B";
  return os.str();
}
}  // namespace

std::string format_pull_summary(const PullResult &r, const ImageRef &ref,
                                  const std::string &bundle_dir) {
  std::ostringstream os;
  os << "Pulled " << ref.host << "/" << ref.name;
  if (!ref.tag.empty())    os << ":" << ref.tag;
  if (!ref.digest.empty()) os << "@" << ref.digest;
  if (!r.selected_platform.empty()) os << " (" << r.selected_platform << ")";
  os << ", " << r.layers_applied << " layer"
     << (r.layers_applied == 1 ? "" : "s")
     << ", " << format_size(r.bytes_downloaded)
     << ", into " << bundle_dir;
  return os.str();
}

// ── usage + version ──────────────────────────────────────────────────────

std::string format_usage() {
  return
      "Usage:\n"
      "  xpmile-pull <image:tag> --to <bundle-dir> [flags]\n"
      "\n"
      "Required:\n"
      "  <image>              Image reference (e.g. naushada/foo:latest)\n"
      "  --to <path>          Output OCI bundle directory\n"
      "\n"
      "Optional:\n"
      "  --arch <a>           Override platform (amd64 / arm64 / arm/v7)\n"
      "  --force              Overwrite an existing bundle\n"
      "  --max-entry-size <N> Per-entry size limit (e.g. 4G)\n"
      "  --max-total-size <N> Total bundle size limit\n"
      "  -h, --help           Print this help\n"
      "  -V, --version        Print version\n";
}

std::string format_version() {
  // Phase H will replace this with a build-injected release tag.
  return "xpmile-pull dev";
}

}  // namespace runcpull
