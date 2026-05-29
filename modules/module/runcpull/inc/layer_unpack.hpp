#ifndef RUNCPULL_LAYER_UNPACK_HPP
#define RUNCPULL_LAYER_UNPACK_HPP

/**
 * @file layer_unpack.hpp
 * @brief Tar parsing + gzip inflate + OCI whiteout-aware rootfs extraction.
 *
 * Phase D of the runc-pull TDD plan (docs/design/runc-pull/runc-pull-tdd-plan.md
 * §"Phase D — Layer unpack"). Three components live here:
 *
 *  - @ref runcpull::TarReader     — POSIX ustar + pax extension headers.
 *                                   Header-only-ish parser; no external dep.
 *  - @ref runcpull::GzipInflater  — Streaming zlib inflate. Uses already-linked
 *                                   libz (no libarchive).
 *  - @ref runcpull::LayerUnpacker — Applies a sequence of layer tar streams to
 *                                   an output rootfs honouring the OCI whiteout
 *                                   spec and the hardening invariants from
 *                                   docs/design/runc-pull/runc-pull-design.md
 *                                   §6.
 *
 * Hardening: safe filesystem writes go through @c openat / @c mkdirat /
 * @c symlinkat / @c linkat from a held root @c dirfd with @c O_NOFOLLOW on
 * every hop — the unpacker never opens through a symlink it just extracted.
 * Path-traversal entries (`..` segments after normalisation, escaping
 * symlinks) are rejected. setuid + setgid + sticky bits are masked off.
 * uid/gid are clamped to the caller's effective uid/gid (rootless policy).
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace runcpull {

/// Error code carried in @ref UnpackResult and accessible via
/// @ref TarReader::error(). NONE indicates success.
enum class UnpackError {
  NONE = 0,

  // Tar parsing.
  TAR_TRUNCATED,
  TAR_BAD_CHECKSUM,
  TAR_UNKNOWN_TYPEFLAG,
  TAR_BAD_PAX,

  // Gzip inflate.
  GZIP_INVALID,
  GZIP_TRUNCATED,

  // Path hardening.
  PATH_TRAVERSAL,           ///< entry name contains `..` after normalisation
  ABSOLUTE_PATH_OUTSIDE,    ///< entry name starts with '/'; stripped to relative
  SYMLINK_ESCAPE,           ///< symlink target resolves outside the rootfs
  WRITE_THROUGH_SYMLINK,    ///< write would follow an existing symlink

  // Size limits.
  PER_ENTRY_SIZE_EXCEEDED,
  TOTAL_SIZE_EXCEEDED,

  // I/O.
  IO_ERROR,
};

/// Parsed tar header. Holds enough for ustar + pax-extended entries.
struct TarEntry {
  enum Type {
    REGULAR,
    DIRECTORY,
    SYMLINK,
    HARDLINK,
    OTHER,            ///< char device, block device, fifo, etc. — we skip these
  };

  std::string name;
  Type        type     = REGULAR;
  std::uint64_t size   = 0;     ///< for REGULAR
  std::uint32_t mode   = 0;     ///< low 12 bits; setuid/setgid/sticky bits
                                ///< NOT yet stripped — LayerUnpacker does that
  std::uint64_t mtime  = 0;
  std::uint32_t uid    = 0;
  std::uint32_t gid    = 0;
  std::string linkname;         ///< target for SYMLINK / HARDLINK
  std::string content;          ///< size bytes of body for REGULAR (in-memory)
};

/**
 * @brief POSIX ustar + pax extension-header parser over an in-memory byte
 *        range.
 *
 * Usage:
 *
 *     TarReader r(tar_bytes);
 *     TarEntry e;
 *     while (r.next(e)) { … }
 *     if (r.error() != UnpackError::NONE) { … }
 *
 * Recognises `pax` extended headers (typeflag `'x'`) that carry `path=…` and
 * `linkpath=…` records — used by Docker layers when names exceed the 100-byte
 * ustar limit. Other pax records are ignored.
 */
class TarReader {
public:
  explicit TarReader(std::string_view bytes);

  /// Advance to the next entry. Returns true on success and fills @p out;
  /// returns false at end-of-archive (double zero block) or on parse error.
  /// Check @ref error() to distinguish.
  bool next(TarEntry &out);

  UnpackError error() const { return m_error; }

private:
  std::string_view m_bytes;
  std::size_t      m_pos    = 0;
  UnpackError      m_error  = UnpackError::NONE;

  // Pax-extended-header values that override the next ustar entry.
  std::string      m_pax_path;
  std::string      m_pax_linkpath;
};

/// Convenience: parse every entry from @p bytes. Returns the entries plus
/// the terminal error (NONE on full parse).
struct TarParseAll {
  std::vector<TarEntry> entries;
  UnpackError           error = UnpackError::NONE;
};
TarParseAll parse_tar(std::string_view bytes);

/**
 * @brief Streaming gzip inflater over in-memory chunks.
 *
 * Feed compressed bytes via @ref write(); pull decompressed bytes via
 * the @p out parameter. Sticky error state — once @ref error() is non-NONE
 * subsequent writes are no-ops.
 *
 * Thin wrapper around zlib's `inflate()`. RAII over `z_stream`.
 */
class GzipInflater {
public:
  GzipInflater();
  ~GzipInflater();

  GzipInflater(const GzipInflater &)            = delete;
  GzipInflater &operator=(const GzipInflater &) = delete;

  /// Push @p len bytes of compressed input. Decompressed output is appended
  /// to @p out. Returns the current error state.
  UnpackError write(const char *data, std::size_t len, std::string &out);

  UnpackError error() const { return m_error; }
  bool        finished() const { return m_done; }

private:
  struct Impl;
  Impl *m_impl;   // pimpl so the public header doesn't pull in <zlib.h>
  UnpackError m_error = UnpackError::NONE;
  bool        m_done  = false;
};

/// Decompress @p gz_bytes in one shot. Returns the inflated bytes plus the
/// terminal error (NONE on success).
struct GunzipAll {
  std::string  bytes;
  UnpackError  error = UnpackError::NONE;
};
GunzipAll gunzip(std::string_view gz_bytes);

/// Options that tune @ref LayerUnpacker behaviour. Defaults are the safe
/// production values; tests typically override the size limits and the
/// metadata sidecar.
struct UnpackOptions {
  bool record_uidgid_metadata = true;
  /// Per-entry cap (default 4 GiB). Set lower in tests for size-limit checks.
  std::uint64_t max_entry_size = 4ULL * 1024 * 1024 * 1024;
  /// Total cap across all entries in a layer (default 4 GiB).
  std::uint64_t max_total_size = 4ULL * 1024 * 1024 * 1024;
  /// Strip setuid + setgid + sticky bits from extracted file modes.
  bool strip_special_modes = true;
  /// Clamp uid/gid on disk to the caller's effective uid/gid (rootless
  /// policy). When false, ownership comes straight from the tar header.
  bool clamp_uid_gid = true;
};

/// Result of @ref LayerUnpacker::apply_layer_tar.
struct UnpackResult {
  UnpackError    error = UnpackError::NONE;
  std::uint64_t  entries_applied = 0;
  std::uint64_t  bytes_written   = 0;
  std::string    detail;          ///< on error, the offending entry name etc.
};

/**
 * @brief Apply a sequence of OCI layer tar streams to an output rootfs.
 *
 * Each @ref apply_layer_tar call extracts one layer on top of the existing
 * rootfs state, honouring the OCI whiteout spec:
 *
 *   - @c .wh.X removes @c X from earlier layers; the marker file itself is
 *     NOT written to @c rootfs.
 *   - @c .wh..wh..opq inside dir @c D removes everything under @c D from
 *     earlier layers, then proceeds with this layer's entries normally.
 *
 * Safe-write invariants (Phase D hardening tests):
 *   - tar entry names with `..` segments after normalisation → rejected.
 *   - absolute paths (`/etc/…`) → stripped to relative (operators expect this).
 *   - symlinks whose targets escape the rootfs → rejected.
 *   - subsequent writes never follow existing symlinks (`O_NOFOLLOW` on
 *     every `openat`).
 *   - setuid + setgid + sticky bits stripped from regular files.
 *   - uid/gid clamped to caller's effective uid/gid by default.
 *   - per-entry + total size limits enforced; abort mid-layer on overflow.
 */
class LayerUnpacker {
public:
  LayerUnpacker(const std::string &rootfs_path, UnpackOptions opts = {});

  UnpackResult apply_layer_tar(std::string_view tar_bytes);
  UnpackResult apply_layer_tar_gz(std::string_view tar_gz_bytes);

private:
  std::string   m_root;
  UnpackOptions m_opts;
};

}  // namespace runcpull

#endif  // RUNCPULL_LAYER_UNPACK_HPP
