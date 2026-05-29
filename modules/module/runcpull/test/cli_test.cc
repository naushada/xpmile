// Phase G — CLI parser + exit-code mapping (14 tests).

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "cli.hpp"
#include "pull_orchestrator.hpp"
#include "registry_client.hpp"

using runcpull::CliArgs;
using runcpull::cli_parse_error_to_exit_code;
using runcpull::CliParseError;
using runcpull::CliParseResult;
using runcpull::format_pull_summary;
using runcpull::format_usage;
using runcpull::format_version;
using runcpull::HostArch;
using runcpull::ImageRef;
using runcpull::parse_args;
using runcpull::parse_size_with_units;
using runcpull::PullError;
using runcpull::pull_error_to_exit_code;
using runcpull::PullResult;

namespace {

// Build an argv array from a list of string literals. The returned
// pointers are valid for the lifetime of the @ref backing vector.
struct Argv {
  std::vector<std::string> backing;
  std::vector<char *>      ptrs;
  int                       argc = 0;

  explicit Argv(std::initializer_list<const char *> tokens) {
    backing.reserve(tokens.size());
    for (const char *t : tokens) backing.emplace_back(t);
    for (auto &s : backing) ptrs.push_back(s.data());
    argc = static_cast<int>(ptrs.size());
  }
  char *const *argv() { return ptrs.data(); }
};

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════
// G.1 — Argument parsing + flags (7 tests)
// ═══════════════════════════════════════════════════════════════════════════

TEST(CliTest, MissingRequiredArgs_Exit6_AndUsageOnStderr) {
  // No positional, no --to.
  Argv a({"xpmile-pull"});
  CliParseResult r = parse_args(a.argc, a.argv());
  EXPECT_NE(r.error, CliParseError::NONE);
  EXPECT_EQ(cli_parse_error_to_exit_code(r.error), 6);
  EXPECT_FALSE(format_usage().empty());
}

TEST(CliTest, HelpFlag_Exit0_AndUsageOnStdout) {
  Argv a({"xpmile-pull", "--help"});
  CliParseResult r = parse_args(a.argc, a.argv());
  EXPECT_EQ(r.error, CliParseError::NONE);
  EXPECT_TRUE(r.args.help);
  EXPECT_EQ(cli_parse_error_to_exit_code(r.error), 0);
  EXPECT_NE(format_usage().find("xpmile-pull"), std::string::npos);
}

TEST(CliTest, VersionFlag_Exit0_AndPrintsVersion) {
  Argv a({"xpmile-pull", "--version"});
  CliParseResult r = parse_args(a.argc, a.argv());
  EXPECT_EQ(r.error, CliParseError::NONE);
  EXPECT_TRUE(r.args.version);
  EXPECT_FALSE(format_version().empty());
}

TEST(CliTest, ArchOverride_PropagatesToOrchestrator) {
  Argv a({"xpmile-pull", "naushada/foo:latest", "--to", "/tmp/bundle",
          "--arch", "arm64"});
  CliParseResult r = parse_args(a.argc, a.argv());
  ASSERT_EQ(r.error, CliParseError::NONE);
  EXPECT_EQ(r.args.arch.arch, HostArch::ARM64);
  EXPECT_EQ(r.args.arch.name, "arm64");

  // The to_pull_options bridge honours the override.
  runcpull::HostArchResult fallback;
  fallback.arch = HostArch::AMD64;
  fallback.name = "amd64";
  auto opts = r.args.to_pull_options(fallback);
  EXPECT_EQ(opts.target.arch, HostArch::ARM64);
}

TEST(CliTest, ToFlag_PropagatesToOrchestrator) {
  Argv a({"xpmile-pull", "naushada/foo", "--to", "/var/lib/xpmile/bundles/foo"});
  CliParseResult r = parse_args(a.argc, a.argv());
  ASSERT_EQ(r.error, CliParseError::NONE);
  EXPECT_EQ(r.args.to_dir, "/var/lib/xpmile/bundles/foo");
}

TEST(CliTest, ForceFlag_PropagatesToBundleWriter) {
  Argv a({"xpmile-pull", "naushada/foo", "--to", "/tmp/b", "--force"});
  CliParseResult r = parse_args(a.argc, a.argv());
  ASSERT_EQ(r.error, CliParseError::NONE);
  EXPECT_TRUE(r.args.force);

  runcpull::HostArchResult fb;
  auto opts = r.args.to_pull_options(fb);
  EXPECT_TRUE(opts.force_overwrite);
}

TEST(CliTest, MaxEntrySize_ParsedWithUnits) {
  EXPECT_EQ(parse_size_with_units("1024"), 1024u);
  EXPECT_EQ(parse_size_with_units("4K"),   4ULL * 1024);
  EXPECT_EQ(parse_size_with_units("2M"),   2ULL * 1024 * 1024);
  EXPECT_EQ(parse_size_with_units("1G"),   1ULL << 30);
  EXPECT_EQ(parse_size_with_units("3T"),   3ULL * (1ULL << 40));
  EXPECT_EQ(parse_size_with_units("8GB"),  8ULL << 30);

  // And the flag wires it through.
  Argv a({"xpmile-pull", "naushada/foo", "--to", "/tmp/b",
          "--max-entry-size", "1G"});
  CliParseResult r = parse_args(a.argc, a.argv());
  ASSERT_EQ(r.error, CliParseError::NONE);
  EXPECT_EQ(r.args.max_entry_size, 1ULL << 30);
}

// ═══════════════════════════════════════════════════════════════════════════
// G.2 — PullError → exit code mapping (7 tests)
// ═══════════════════════════════════════════════════════════════════════════

TEST(CliTest, ExitCode_Transport_Is1) {
  EXPECT_EQ(pull_error_to_exit_code(PullError::TRANSPORT),       1);
  EXPECT_EQ(pull_error_to_exit_code(PullError::BLOB_NOT_FOUND),  1);
  EXPECT_EQ(pull_error_to_exit_code(PullError::REDIRECT_LIMIT),  1);
}

TEST(CliTest, ExitCode_Auth_Is2) {
  EXPECT_EQ(pull_error_to_exit_code(PullError::AUTH), 2);
}

TEST(CliTest, ExitCode_Manifest_Is3) {
  EXPECT_EQ(pull_error_to_exit_code(PullError::MANIFEST_NOT_FOUND),    3);
  EXPECT_EQ(pull_error_to_exit_code(PullError::MANIFEST_PARSE),        3);
  EXPECT_EQ(pull_error_to_exit_code(PullError::NO_MATCHING_PLATFORM),  3);
  EXPECT_EQ(pull_error_to_exit_code(PullError::IMAGE_CONFIG_PARSE),    3);
}

TEST(CliTest, ExitCode_DigestMismatch_Is4) {
  EXPECT_EQ(pull_error_to_exit_code(PullError::CONFIG_DIGEST_MISMATCH), 4);
  EXPECT_EQ(pull_error_to_exit_code(PullError::LAYER_DIGEST_MISMATCH),  4);
}

TEST(CliTest, ExitCode_Unpack_Is5) {
  EXPECT_EQ(pull_error_to_exit_code(PullError::UNPACK_FAILED), 5);
}

TEST(CliTest, ExitCode_Usage_Is6) {
  EXPECT_EQ(cli_parse_error_to_exit_code(CliParseError::MISSING_IMAGE), 6);
  EXPECT_EQ(cli_parse_error_to_exit_code(CliParseError::MISSING_TO),    6);
  EXPECT_EQ(cli_parse_error_to_exit_code(CliParseError::UNKNOWN_FLAG),  6);
  EXPECT_EQ(cli_parse_error_to_exit_code(CliParseError::BAD_VALUE),     6);
  EXPECT_EQ(cli_parse_error_to_exit_code(CliParseError::NONE),          0);
}

TEST(CliTest, Success_Is0_AndPrintsSummaryLine) {
  EXPECT_EQ(pull_error_to_exit_code(PullError::NONE), 0);

  ImageRef ref{"docker.io", "naushada/foo", "latest", ""};
  PullResult r;
  r.error             = PullError::NONE;
  r.layers_applied    = 3;
  r.bytes_downloaded  = 198ULL * 1024 * 1024;
  r.selected_platform = "linux/arm64";

  const std::string s = format_pull_summary(r, ref, "/var/lib/xpmile/bundles/foo");
  EXPECT_NE(s.find("docker.io/naushada/foo:latest"), std::string::npos);
  EXPECT_NE(s.find("3 layers"), std::string::npos);
  EXPECT_NE(s.find("linux/arm64"), std::string::npos);
  EXPECT_NE(s.find("/var/lib/xpmile/bundles/foo"), std::string::npos);
}
