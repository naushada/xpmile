#ifndef RUNCPULL_CLI_HPP
#define RUNCPULL_CLI_HPP

/**
 * @file cli.hpp
 * @brief CLI plumbing for the xpmile-pull binary.
 *
 * Phase G of the runc-pull TDD plan (docs/design/runc-pull/runc-pull-tdd-plan.md
 * §"Phase G"). Pure-function pieces:
 *
 *   - @ref runcpull::parse_args            — argv → @ref CliArgs
 *   - @ref runcpull::pull_error_to_exit_code
 *                                          — @ref PullError → POSIX exit code
 *   - @ref runcpull::cli_parse_error_to_exit_code
 *                                          — usage errors → exit code 6
 *   - @ref runcpull::format_pull_summary   — one-line success summary
 *
 * The actual main() that calls into these lives in `main.cpp` (Phase H).
 * That file is only built into the xpmile-pull executable target; the
 * test binary `offtarget` doesn't link it.
 */

#include <cstdint>
#include <string>

#include "pull_orchestrator.hpp"
#include "registry_client.hpp"

namespace runcpull {

// ── CliArgs ─────────────────────────────────────────────────────────────

/// Parsed command-line arguments. A `parse_args()` call yielding
/// @c CliParseError::NONE guarantees all required fields are populated.
struct CliArgs {
  // Flags that short-circuit the run.
  bool help    = false;
  bool version = false;

  // Required positionals + named args.
  std::string image;       ///< image reference, e.g. "naushada/foo:latest"
  std::string to_dir;      ///< --to bundle dir target

  // Optional knobs.
  bool           force                = false;  ///< --force
  std::uint64_t  max_entry_size       = 0;      ///< --max-entry-size; 0 = leave default
  std::uint64_t  max_total_size       = 0;      ///< --max-total-size
  HostArchResult arch;                          ///< --arch override; default UNKNOWN

  /// Translate the args into orchestrator options. Caller passes a probe
  /// for the auto-detect path when @ref arch.arch is UNKNOWN.
  PullOptions to_pull_options(const HostArchResult &fallback) const;
};

/// Parse failure surface. Mapped to exit code 6 by the CLI.
enum class CliParseError {
  NONE = 0,
  MISSING_IMAGE,
  MISSING_TO,
  UNKNOWN_FLAG,
  BAD_VALUE,
};

struct CliParseResult {
  CliArgs       args;
  CliParseError error = CliParseError::NONE;
  std::string   detail;     ///< for BAD_VALUE / UNKNOWN_FLAG: the offending token
};

/**
 * @brief Walk argv and build a @ref CliArgs.
 *
 * Accepted flags:
 *   - `--to <path>`              ; required
 *   - `--arch <amd64|arm64|...>` ; optional override (else auto-detect at main)
 *   - `--force`                  ; allow overwrite of existing bundle
 *   - `--max-entry-size <N>`     ; N with optional K/M/G/T suffix
 *   - `--max-total-size <N>`     ; same
 *   - `-h` / `--help`            ; print usage and exit 0
 *   - `-V` / `--version`         ; print version and exit 0
 *
 * Positional: exactly one image reference. Both `--help` and `--version`
 * suppress the missing-arg check.
 */
CliParseResult parse_args(int argc, char *const argv[]);

/// Parse a size value like "1G", "512M", "1024", returning the byte count
/// or 0 on parse failure. Suffixes: K=1024, M=1024², G=1024³, T=1024⁴.
std::uint64_t parse_size_with_units(const std::string &s);

/**
 * @brief Map a @ref PullError to its CLI exit code.
 *
 * Stable contract (see runc-pull-tdd-plan.md §11 "Failure modes & UX"):
 *   - 0 — success
 *   - 1 — transport / network failure
 *   - 2 — auth (token endpoint refused, bad challenge)
 *   - 3 — manifest (parse, not-found, no-matching-arch, registry mismatch)
 *   - 4 — digest mismatch (config or layer)
 *   - 5 — unpack failure (path traversal, size limit, gzip/tar error)
 *   - 6 — usage (argv parsing)
 */
int pull_error_to_exit_code(PullError e);

/// Map a usage error to exit code 6. Helper for symmetry with the
/// PullError mapper above.
int cli_parse_error_to_exit_code(CliParseError e);

/**
 * @brief One-line success summary printed to stdout on PullError::NONE.
 *
 * Example: `"Pulled docker.io/naushada/foo:latest (linux/arm64), 3 layers,
 *           198 MB, into /var/lib/xpmile/bundles/foo"`.
 *
 * Pure function — no I/O — so tests assert on the returned string.
 */
std::string format_pull_summary(const PullResult &r, const ImageRef &ref,
                                  const std::string &bundle_dir);

/// Returns the canonical usage block printed by `--help` and on parse
/// errors. Newline-terminated.
std::string format_usage();

/// Returns the version string printed by `--version`. Single line, no
/// trailing newline. Today: hardcoded; Phase H will inject the release
/// tag at build time.
std::string format_version();

}  // namespace runcpull

#endif  // RUNCPULL_CLI_HPP
