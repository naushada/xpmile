#include "bundle_writer.hpp"

#include <cstdio>
#include <fstream>
#include <stdexcept>

#include "json.hpp"

namespace runcpull {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

/// Build the stub `config.json` document. Only the image-config-derived
/// fields are populated; mounts and namespaces are left empty for the
/// operator to overwrite from the per-service template
/// (docs/operator-runc.md §5).
json build_stub_config(const std::vector<std::string> &env,
                       const std::vector<std::string> &entrypoint,
                       const std::vector<std::string> &cmd) {
  // process.args = entrypoint ++ cmd (Docker / OCI image-config semantics).
  json args = json::array();
  for (const auto &s : entrypoint) {
    args.push_back(s);
  }
  for (const auto &s : cmd) {
    args.push_back(s);
  }

  json process_env = json::array();
  for (const auto &s : env) {
    process_env.push_back(s);
  }

  json doc = {
      {"ociVersion", kStubOciVersion},
      {"process",
       {
           {"terminal", false},
           {"user", {{"uid", 0}, {"gid", 0}}},
           {"args", args},
           {"env", process_env},
           {"cwd", "/"},
       }},
      {"root", {{"path", "rootfs"}, {"readonly", false}}},
      {"mounts", json::array()},
      {"linux", {{"namespaces", json::array()}}},
  };
  return doc;
}

/// Best-effort recursive remove, swallowing errors. Used in failure-cleanup
/// paths where we already have an exception in flight.
void best_effort_remove_all(const fs::path &p) noexcept {
  std::error_code ignored;
  fs::remove_all(p, ignored);
}

} // namespace

void BundleWriter::write(const fs::path &bundle_dir,
                          const std::vector<std::string> &env,
                          const std::vector<std::string> &entrypoint,
                          const std::vector<std::string> &cmd,
                          const BundleWriterOptions &options) {
  // ── 1. Refuse-overwrite gate (unless --force). ────────────────────────────
  if (fs::exists(bundle_dir)) {
    if (!options.force) {
      throw std::runtime_error(
          "bundle directory already exists (pass force=true to overwrite): " +
          bundle_dir.string());
    }
  }

  // ── 2. Prepare a sibling tempdir. ─────────────────────────────────────────
  // Naming: `<bundle_dir>.tmp`. If a previous run was interrupted and left
  // one behind, blow it away first — we own this name.
  fs::path tempdir = bundle_dir;
  tempdir += ".tmp";
  best_effort_remove_all(tempdir);

  try {
    fs::create_directories(tempdir);

    // ── 3. rootfs/ — empty for now. Phase D's unpacker fills it. ─────────────
    fs::create_directories(tempdir / "rootfs");

    // ── 4. config.json — the stub. ──────────────────────────────────────────
    json doc = build_stub_config(env, entrypoint, cmd);
    {
      std::ofstream out(tempdir / "config.json");
      if (!out) {
        throw std::runtime_error("failed to open config.json for writing in " +
                                 tempdir.string());
      }
      out << doc.dump(2);
      out.flush();
      if (!out) {
        throw std::runtime_error("failed to write config.json in " +
                                 tempdir.string());
      }
    }

    // ── 4b. Test hook: inject a mid-write failure to exercise atomicity. ───
    if (!options.fail_after_writing_relpath.empty()) {
      const fs::path probe =
          tempdir / options.fail_after_writing_relpath;
      fs::create_directories(probe.parent_path());
      std::ofstream sentinel(probe);
      sentinel << "injected-failure";
      sentinel.flush();
      throw std::runtime_error(
          "injected failure after writing " +
          options.fail_after_writing_relpath +
          " (BundleWriterOptions::fail_after_writing_relpath)");
    }

    // ── 5. Atomic finalisation. ─────────────────────────────────────────────
    // If `force` was set and the target still exists, clear it out before
    // renaming so std::filesystem::rename doesn't refuse on a non-empty dir.
    if (fs::exists(bundle_dir)) {
      best_effort_remove_all(bundle_dir);
    }
    fs::rename(tempdir, bundle_dir);
  } catch (...) {
    best_effort_remove_all(tempdir);
    throw;
  }
}

} // namespace runcpull
