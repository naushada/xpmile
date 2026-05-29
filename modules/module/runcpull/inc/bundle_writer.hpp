#ifndef RUNCPULL_BUNDLE_WRITER_HPP
#define RUNCPULL_BUNDLE_WRITER_HPP

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

/**
 * @file bundle_writer.hpp
 * @brief OCI bundle layout writer — `rootfs/` directory + stub `config.json`.
 *
 * Phase E of the runc-pull TDD plan. After `LayerUnpacker` (Phase D) has
 * extracted every layer into a temporary rootfs, `BundleWriter` finalises
 * the OCI bundle on disk:
 *
 *   <bundle_dir>/
 *     rootfs/      (populated by the unpacker)
 *     config.json  (stub — operator overwrites with the per-service template
 *                   from docs/operator-runc.md §5)
 *
 * The stub `config.json` carries only the bits that come from the image
 * config (the image's `Env`, plus `Entrypoint ++ Cmd` as `process.args`).
 * Mounts and namespaces are intentionally left empty — the operator's
 * per-service template overwrites them.
 *
 * **Atomicity invariant.** All work happens under `<bundle_dir>.tmp/`.
 * The final step is `std::filesystem::rename(<bundle_dir>.tmp, <bundle_dir>)`.
 * If any step before the rename throws / fails, the tempdir is torn down and
 * `<bundle_dir>` does not exist on disk — there is no half-written bundle.
 *
 * **No new dependencies.** JSON is `nlohmann::json` from the project's
 * `thirdparty/json.hpp`. Filesystem operations are C++17 `<filesystem>`.
 */
namespace runcpull {

/// Options for `BundleWriter::write`. Default-constructed = "fail if the
/// target bundle directory already exists".
struct BundleWriterOptions {
  /// When true, an existing `bundle_dir` is removed atomically before the
  /// rename. Mirrors the CLI `--force` flag.
  bool force = false;

  /// **Testing hook.** When non-empty, the named file inside the tempdir is
  /// created and then a synthetic failure is raised mid-write so the test
  /// can verify the atomic-rename invariant. Production callers leave this
  /// empty.
  std::string fail_after_writing_relpath;
};

/**
 * @brief Writes the OCI bundle layout for a pulled image.
 *
 * The writer is stateless — construct, call `write()`, discard. The
 * `write(...)` overload below takes the three image-config fields directly
 * rather than a `const ImageConfig&` reference. Rationale: keeps Phase E's
 * unit tests free of any compile-time coupling to Phase C's `ImageConfig`
 * struct, which is owned by a different developer / worktree. Phase F
 * (the orchestrator) bridges the two — it reads `ImageConfig` and forwards
 * the three fields here.
 */
class BundleWriter {
public:
  BundleWriter() = default;

  /**
   * @brief Materialise the bundle at @p bundle_dir.
   *
   * @param bundle_dir  Final on-disk location. Parent directory must exist.
   * @param env         Image config `Env` — every element is a `KEY=VALUE`
   *                    string, written verbatim into `process.env`.
   * @param entrypoint  Image config `Entrypoint` — first segment of
   *                    `process.args`.
   * @param cmd         Image config `Cmd` — appended to `process.args`.
   * @param options     `force=true` allows overwrite; the test hook can
   *                    inject a mid-write failure.
   *
   * The caller is responsible for populating `<bundle_dir>/rootfs/` after
   * `write()` returns — that's Phase D's job, and `write()` only creates
   * the (empty) `rootfs/` directory the unpacker will fill. (In the
   * orchestrator the writer is invoked BEFORE the unpacker so the unpacker
   * can extract straight into `<bundle_dir>/rootfs/`.)
   *
   * Throws `std::filesystem::filesystem_error` / `std::runtime_error` on
   * I/O failure. On any failure path the tempdir is removed and
   * `bundle_dir` is left untouched on disk — there is no partial bundle.
   */
  void write(const std::filesystem::path &bundle_dir,
             const std::vector<std::string> &env,
             const std::vector<std::string> &entrypoint,
             const std::vector<std::string> &cmd,
             const BundleWriterOptions &options = {});
};

/// OCI runtime-spec version the stub `config.json` declares. Pinned to
/// the version `runc` 1.1.x ships with on Debian Bookworm — the operator
/// will overwrite the file anyway, so this is a sensible default rather
/// than a strict requirement.
inline constexpr const char *kStubOciVersion = "1.0.2-dev";

} // namespace runcpull

#endif // RUNCPULL_BUNDLE_WRITER_HPP
