/**
 * @file bundle_writer_test.cc
 * @brief GTest suite for `runcpull::BundleWriter` (Phase E.1, 8 tests).
 *
 * Every test runs entirely inside `testing::TempDir()` — no writes outside.
 * Fixtures are constructed in C++; nothing is checked into git here.
 */

#include "bundle_writer.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "json.hpp"

namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

/// Returns a fresh, empty per-test directory under `testing::TempDir()`.
/// The directory itself is the *parent* in which the test then creates
/// `<bundle_dir>` — never the bundle dir itself.
fs::path make_test_parent_dir(const std::string &tag) {
  static std::atomic<uint64_t> seq{0};
  const uint64_t n = seq.fetch_add(1, std::memory_order_relaxed);
  fs::path p = fs::path(testing::TempDir()) /
               ("bundle_writer_test_" + tag + "_" + std::to_string(n));
  std::error_code ec;
  fs::remove_all(p, ec); // start clean even if a previous run left debris
  fs::create_directories(p);
  return p;
}

/// Reads `config.json` from a finished bundle and parses it. ASSERT_*-style
/// failures in here propagate via gtest's death-on-fatal semantics; callers
/// must use ASSERT_NO_FATAL_FAILURE() if they want to bail early.
json load_config_json(const fs::path &bundle_dir) {
  std::ifstream in(bundle_dir / "config.json");
  EXPECT_TRUE(in.good()) << "config.json missing in " << bundle_dir;
  std::stringstream ss;
  ss << in.rdbuf();
  return json::parse(ss.str(), /*cb*/ nullptr, /*allow_exceptions*/ false);
}

} // namespace

// ── Test 1 — rootfs/ + valid config.json. ────────────────────────────────────
TEST(BundleWriterTest, CreatesRootfsAndStubConfig) {
  const fs::path parent = make_test_parent_dir("creates");
  const fs::path bundle = parent / "bundle";

  runcpull::BundleWriter w;
  w.write(bundle, /*env=*/{}, /*entrypoint=*/{}, /*cmd=*/{});

  ASSERT_TRUE(fs::exists(bundle)) << "bundle dir not created";
  EXPECT_TRUE(fs::is_directory(bundle / "rootfs")) << "rootfs/ missing";
  ASSERT_TRUE(fs::is_regular_file(bundle / "config.json"))
      << "config.json missing";

  json doc = load_config_json(bundle);
  EXPECT_FALSE(doc.is_discarded()) << "config.json is not valid JSON";
}

// ── Test 2 — config.json carries an OCI version. ─────────────────────────────
TEST(BundleWriterTest, StubConfig_HasOciVersion) {
  const fs::path parent = make_test_parent_dir("ociver");
  const fs::path bundle = parent / "bundle";

  runcpull::BundleWriter().write(bundle, {}, {}, {});

  json doc = load_config_json(bundle);
  ASSERT_FALSE(doc.is_discarded());
  ASSERT_TRUE(doc.contains("ociVersion"));
  EXPECT_TRUE(doc.at("ociVersion").is_string());
  EXPECT_FALSE(doc.at("ociVersion").get<std::string>().empty());
}

// ── Test 3 — process.env mirrors the image config's Env list. ────────────────
TEST(BundleWriterTest, StubConfig_HasProcessEnv_FromImageConfig) {
  const fs::path parent = make_test_parent_dir("env");
  const fs::path bundle = parent / "bundle";

  const std::vector<std::string> env = {
      "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin",
      "MONGO_VERSION=4.4.18",
      "GOSU_VERSION=1.14",
  };

  runcpull::BundleWriter().write(bundle, env, {}, {});

  json doc = load_config_json(bundle);
  ASSERT_FALSE(doc.is_discarded());
  ASSERT_TRUE(doc.contains("process"));
  ASSERT_TRUE(doc.at("process").contains("env"));
  const auto &out_env = doc.at("process").at("env");
  ASSERT_TRUE(out_env.is_array());
  ASSERT_EQ(out_env.size(), env.size());
  for (size_t i = 0; i < env.size(); ++i) {
    EXPECT_EQ(out_env.at(i).get<std::string>(), env[i]);
  }
}

// ── Test 4 — process.args = Entrypoint ++ Cmd. ───────────────────────────────
TEST(BundleWriterTest, StubConfig_HasProcessArgs_EntrypointThenCmd) {
  const fs::path parent = make_test_parent_dir("args");
  const fs::path bundle = parent / "bundle";

  const std::vector<std::string> entrypoint = {"/bin/sh", "-c"};
  const std::vector<std::string> cmd = {"mongod"};

  runcpull::BundleWriter().write(bundle, /*env=*/{}, entrypoint, cmd);

  json doc = load_config_json(bundle);
  ASSERT_FALSE(doc.is_discarded());
  ASSERT_TRUE(doc.contains("process"));
  ASSERT_TRUE(doc.at("process").contains("args"));
  const auto &args = doc.at("process").at("args");
  ASSERT_TRUE(args.is_array());
  ASSERT_EQ(args.size(), 3u);
  EXPECT_EQ(args.at(0).get<std::string>(), "/bin/sh");
  EXPECT_EQ(args.at(1).get<std::string>(), "-c");
  EXPECT_EQ(args.at(2).get<std::string>(), "mongod");
}

// ── Test 5 — empty mounts + empty namespaces (operator overwrites). ──────────
TEST(BundleWriterTest, StubConfig_EmptyMounts_EmptyNamespaces) {
  const fs::path parent = make_test_parent_dir("empty");
  const fs::path bundle = parent / "bundle";

  runcpull::BundleWriter().write(bundle, {}, {}, {});

  json doc = load_config_json(bundle);
  ASSERT_FALSE(doc.is_discarded());

  ASSERT_TRUE(doc.contains("mounts"));
  EXPECT_TRUE(doc.at("mounts").is_array());
  EXPECT_EQ(doc.at("mounts").size(), 0u);

  ASSERT_TRUE(doc.contains("linux"));
  ASSERT_TRUE(doc.at("linux").contains("namespaces"));
  EXPECT_TRUE(doc.at("linux").at("namespaces").is_array());
  EXPECT_EQ(doc.at("linux").at("namespaces").size(), 0u);
}

// ── Test 6 — mid-write failure must leave no bundle on disk. ─────────────────
TEST(BundleWriterTest, AtomicRename_PartialFailureLeavesNoBundle) {
  const fs::path parent = make_test_parent_dir("atomic");
  const fs::path bundle = parent / "bundle";

  runcpull::BundleWriterOptions opts;
  opts.fail_after_writing_relpath = "rootfs/.injected-marker";

  EXPECT_THROW(runcpull::BundleWriter().write(bundle, {}, {}, {}, opts),
               std::runtime_error);

  // Atomicity invariant: the final bundle dir must not exist on disk.
  EXPECT_FALSE(fs::exists(bundle))
      << "bundle directory survived a partial-write failure";

  // And the .tmp scratch dir must also be cleaned up — no orphan left over.
  fs::path scratch = bundle;
  scratch += ".tmp";
  EXPECT_FALSE(fs::exists(scratch))
      << "scratch tempdir was not cleaned up after failure";
}

// ── Test 7 — refuse to overwrite an existing bundle by default. ──────────────
TEST(BundleWriterTest, RefusesToOverwriteExistingBundle_WithoutForce) {
  const fs::path parent = make_test_parent_dir("refuse");
  const fs::path bundle = parent / "bundle";

  // First write — succeeds.
  runcpull::BundleWriter().write(bundle, {}, {"/bin/true"}, {});
  ASSERT_TRUE(fs::is_regular_file(bundle / "config.json"));

  // Drop a sentinel that proves the second write didn't silently clobber.
  {
    std::ofstream sent(bundle / "sentinel.txt");
    sent << "do-not-overwrite-me";
  }

  // Second write without `force` must refuse.
  EXPECT_THROW(runcpull::BundleWriter().write(bundle, {}, {"/bin/true"}, {}),
               std::runtime_error);

  // And the original bundle (including the sentinel) is untouched.
  EXPECT_TRUE(fs::exists(bundle / "sentinel.txt"))
      << "sentinel file disappeared — overwrite happened anyway";
}

// ── Test 8 — `force = true` overwrites atomically. ───────────────────────────
TEST(BundleWriterTest, ForceFlag_OverwritesAtomically) {
  const fs::path parent = make_test_parent_dir("force");
  const fs::path bundle = parent / "bundle";

  // First write: entrypoint = /bin/old.
  runcpull::BundleWriter().write(bundle, {"FIRST=1"}, {"/bin/old"}, {});
  ASSERT_TRUE(fs::is_regular_file(bundle / "config.json"));
  {
    std::ofstream stale(bundle / "stale.txt");
    stale << "leftover-from-first-write";
  }
  ASSERT_TRUE(fs::exists(bundle / "stale.txt"));

  // Second write with `force = true`: entrypoint = /bin/new.
  runcpull::BundleWriterOptions opts;
  opts.force = true;
  runcpull::BundleWriter().write(bundle, {"SECOND=2"}, {"/bin/new"}, {}, opts);

  // The stale sidecar from the first write must be gone (overwrite was a
  // full directory swap, not a merge).
  EXPECT_FALSE(fs::exists(bundle / "stale.txt"))
      << "force overwrite merged with old bundle instead of replacing it";

  // And the config now reflects the second write.
  json doc = load_config_json(bundle);
  ASSERT_FALSE(doc.is_discarded());
  ASSERT_TRUE(doc.contains("process"));
  ASSERT_TRUE(doc.at("process").contains("args"));
  ASSERT_EQ(doc.at("process").at("args").size(), 1u);
  EXPECT_EQ(doc.at("process").at("args").at(0).get<std::string>(), "/bin/new");

  ASSERT_TRUE(doc.at("process").contains("env"));
  ASSERT_EQ(doc.at("process").at("env").size(), 1u);
  EXPECT_EQ(doc.at("process").at("env").at(0).get<std::string>(), "SECOND=2");

  // And no `.tmp` scratch leftover.
  fs::path scratch = bundle;
  scratch += ".tmp";
  EXPECT_FALSE(fs::exists(scratch));
}
