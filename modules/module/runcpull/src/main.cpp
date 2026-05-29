// xpmile-pull — in-house Docker Hub image puller for the runc operator path.
//
// Wires Phase G's CLI parser to Phase F's orchestrator. The actual work
// happens in the library at modules/module/runcpull/.

#include <cstdio>
#include <cstdlib>
#include <iostream>

#include "cli.hpp"
#include "pull_orchestrator.hpp"
#include "registry_client.hpp"
#include "sso_http_client.hpp"

int main(int argc, char *argv[]) {
  using namespace runcpull;

  // ── 1. Parse argv ───────────────────────────────────────────────────────
  CliParseResult parsed = parse_args(argc, argv);
  if (parsed.args.help) {
    std::cout << format_usage();
    return 0;
  }
  if (parsed.args.version) {
    std::cout << format_version() << "\n";
    return 0;
  }
  if (parsed.error != CliParseError::NONE) {
    std::cerr << "xpmile-pull: " << parsed.detail << "\n\n"
              << format_usage();
    return cli_parse_error_to_exit_code(parsed.error);
  }

  // ── 2. Validate the image reference ─────────────────────────────────────
  RefParseResult ref = parse_ref(parsed.args.image);
  if (ref.error != RefError::NONE) {
    std::cerr << "xpmile-pull: invalid image reference \""
              << parsed.args.image << "\": ";
    switch (ref.error) {
      case RefError::EMPTY:            std::cerr << "empty input";              break;
      case RefError::INVALID_CHARS:    std::cerr << "invalid characters";       break;
      case RefError::NON_HUB_REGISTRY: std::cerr << "non-Hub registry — only "
                                                    "docker.io is supported "
                                                    "in v1"; break;
      default:                          std::cerr << "parse failure";            break;
    }
    std::cerr << "\n";
    return 6;
  }

  // ── 3. Host arch detection (CLI override beats probe — Phase B) ─────────
  HostProbe probe;
  const HostArchResult host = resolve_host_arch(probe);

  // ── 4. Construct the HTTP client and run the pull ───────────────────────
  sso::HttpClient http;
  PullOrchestrator orch(http, parsed.args.to_pull_options(host));
  const PullResult result = orch.run(ref.ref, parsed.args.to_dir);

  // ── 5. Summary / error reporting ────────────────────────────────────────
  if (result.error == PullError::NONE) {
    std::cout << format_pull_summary(result, ref.ref, parsed.args.to_dir) << "\n";
    return 0;
  }

  std::cerr << "xpmile-pull: pull failed (";
  switch (result.error) {
    case PullError::TRANSPORT:                std::cerr << "transport";                break;
    case PullError::AUTH:                     std::cerr << "auth";                     break;
    case PullError::MANIFEST_NOT_FOUND:       std::cerr << "manifest not found";       break;
    case PullError::MANIFEST_PARSE:           std::cerr << "manifest parse";           break;
    case PullError::IMAGE_CONFIG_PARSE:       std::cerr << "image config parse";       break;
    case PullError::NO_MATCHING_PLATFORM:     std::cerr << "no matching platform";     break;
    case PullError::CONFIG_DIGEST_MISMATCH:   std::cerr << "config digest mismatch";   break;
    case PullError::LAYER_DIGEST_MISMATCH:    std::cerr << "layer digest mismatch";    break;
    case PullError::BLOB_NOT_FOUND:           std::cerr << "blob not found";           break;
    case PullError::UNPACK_FAILED:            std::cerr << "unpack failed";            break;
    case PullError::BUNDLE_WRITE_FAILED:      std::cerr << "bundle write failed";      break;
    case PullError::REDIRECT_LIMIT:           std::cerr << "redirect limit";           break;
    case PullError::UNSUPPORTED_REGISTRY:     std::cerr << "unsupported registry";     break;
    case PullError::NONE:                     std::cerr << "none?!";                   break;
  }
  std::cerr << ")";
  if (!result.detail.empty()) std::cerr << ": " << result.detail;
  std::cerr << "\n";
  return pull_error_to_exit_code(result.error);
}
