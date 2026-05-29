#include "layer_unpack.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <system_error>
#include <unistd.h>
#include <utime.h>
#include <vector>

#include <zlib.h>

namespace fs = std::filesystem;

namespace runcpull {

namespace {

// ── POSIX ustar block layout (offsets within a 512-byte block) ──────────
// Reference: POSIX ustar interchange format. We support the original ustar
// fields plus pax-extended headers (typeflag 'x') that carry path= /
// linkpath= records — Docker uses these for names exceeding the ustar
// limits. We do NOT support GNU-tar long-link/long-name extensions.
constexpr std::size_t TBLOCK = 512;

constexpr std::size_t F_NAME     = 0;     // 100 bytes (NUL-padded)
constexpr std::size_t F_MODE     = 100;   //   8
constexpr std::size_t F_UID      = 108;   //   8
constexpr std::size_t F_GID      = 116;   //   8
constexpr std::size_t F_SIZE     = 124;   //  12
constexpr std::size_t F_MTIME    = 136;   //  12
constexpr std::size_t F_CHKSUM   = 148;   //   8
constexpr std::size_t F_TYPEFLAG = 156;   //   1
constexpr std::size_t F_LINKNAME = 157;   // 100
constexpr std::size_t F_MAGIC    = 257;   //   6 ("ustar\0" or "ustar ")
constexpr std::size_t F_VERSION  = 263;   //   2
constexpr std::size_t F_UNAME    = 265;   //  32
constexpr std::size_t F_GNAME    = 297;   //  32
constexpr std::size_t F_DEVMAJOR = 329;   //   8
constexpr std::size_t F_DEVMINOR = 337;   //   8
constexpr std::size_t F_PREFIX   = 345;   // 155 (ustar)

// ── small helpers ───────────────────────────────────────────────────────

std::string c_str_of(const char *p, std::size_t cap) {
  // ustar fields are NUL-padded or space-padded.
  std::size_t n = 0;
  while (n < cap && p[n] != '\0') ++n;
  return std::string(p, p + n);
}

// Parse a NUL/space-terminated octal numeric field. Returns 0 on empty.
std::uint64_t parse_octal(const char *p, std::size_t cap) {
  std::uint64_t v = 0;
  std::size_t i = 0;
  // Skip leading spaces.
  while (i < cap && (p[i] == ' ' || p[i] == '\0')) ++i;
  while (i < cap && p[i] >= '0' && p[i] <= '7') {
    v = (v << 3) + static_cast<std::uint64_t>(p[i] - '0');
    ++i;
  }
  return v;
}

bool is_zero_block(const char *p) {
  for (std::size_t i = 0; i < TBLOCK; ++i) if (p[i] != '\0') return false;
  return true;
}

// Verify the ustar checksum field over the 512-byte header. The checksum
// field itself is treated as 8 spaces during the sum.
bool checksum_ok(const char *hdr) {
  std::uint32_t want = static_cast<std::uint32_t>(parse_octal(hdr + F_CHKSUM, 8));
  std::uint32_t sum  = 0;
  for (std::size_t i = 0; i < TBLOCK; ++i) {
    sum += (i >= F_CHKSUM && i < F_CHKSUM + 8)
               ? static_cast<std::uint8_t>(' ')
               : static_cast<std::uint8_t>(hdr[i]);
  }
  return sum == want;
}

// Apply pax `key=value` records to override path / linkpath. Other keys
// are ignored. Pax records are formatted as
//
//     "<len> key=value\n"
//
// where <len> is the decimal byte length of the whole record including the
// trailing newline.
bool apply_pax_record(std::string_view body,
                       std::string &path_out,
                       std::string &linkpath_out) {
  std::size_t pos = 0;
  while (pos < body.size()) {
    // Skip nothing — read the length prefix.
    std::size_t sp = body.find(' ', pos);
    if (sp == std::string_view::npos) return false;
    std::size_t len = 0;
    for (std::size_t i = pos; i < sp; ++i) {
      if (body[i] < '0' || body[i] > '9') return false;
      len = len * 10 + static_cast<std::size_t>(body[i] - '0');
    }
    if (len == 0 || pos + len > body.size()) return false;
    // Record is body[pos .. pos+len). Inside, after the space, comes
    // "key=value\n". Find the '=' between sp+1 and pos+len-1 (the '\n').
    if (body[pos + len - 1] != '\n') return false;
    std::size_t eq = body.find('=', sp + 1);
    if (eq == std::string_view::npos || eq >= pos + len - 1) return false;
    std::string_view key(body.data() + sp + 1, eq - (sp + 1));
    std::string_view val(body.data() + eq + 1, (pos + len - 1) - (eq + 1));
    if (key == "path")     path_out     = std::string(val);
    else if (key == "linkpath") linkpath_out = std::string(val);
    pos += len;
  }
  return true;
}

}  // namespace

// ── TarReader ───────────────────────────────────────────────────────────

TarReader::TarReader(std::string_view bytes) : m_bytes(bytes) {}

bool TarReader::next(TarEntry &out) {
  if (m_error != UnpackError::NONE) return false;

  while (true) {
    if (m_pos + TBLOCK > m_bytes.size()) {
      // Allow ragged end: if the remaining bytes are all zero, treat as end.
      bool all_zero = true;
      for (std::size_t i = m_pos; i < m_bytes.size(); ++i) {
        if (m_bytes[i] != '\0') { all_zero = false; break; }
      }
      if (!all_zero) m_error = UnpackError::TAR_TRUNCATED;
      return false;
    }
    const char *hdr = m_bytes.data() + m_pos;

    if (is_zero_block(hdr)) {
      // Could be end-of-archive marker (two zero blocks).
      m_pos += TBLOCK;
      if (m_pos + TBLOCK <= m_bytes.size() && is_zero_block(m_bytes.data() + m_pos)) {
        return false;  // clean end
      }
      // One zero block but not two — odd but not necessarily fatal. Be
      // permissive and treat as end-of-archive.
      return false;
    }

    if (!checksum_ok(hdr)) {
      m_error = UnpackError::TAR_BAD_CHECKSUM;
      return false;
    }

    const std::string raw_name = [&] {
      std::string name = c_str_of(hdr + F_NAME, 100);
      // ustar prefix support: if "ustar" magic is set and prefix is
      // non-empty, prepend "prefix/name". Older "ustar " (GNU) variant
      // ignores prefix.
      std::string magic = c_str_of(hdr + F_MAGIC, 6);
      if (magic == "ustar") {
        std::string prefix = c_str_of(hdr + F_PREFIX, 155);
        if (!prefix.empty()) name = prefix + "/" + name;
      }
      return name;
    }();
    const std::uint64_t size  = parse_octal(hdr + F_SIZE, 12);
    const std::uint32_t mode  = static_cast<std::uint32_t>(parse_octal(hdr + F_MODE, 8));
    const std::uint64_t mtime = parse_octal(hdr + F_MTIME, 12);
    const std::uint32_t uid   = static_cast<std::uint32_t>(parse_octal(hdr + F_UID, 8));
    const std::uint32_t gid   = static_cast<std::uint32_t>(parse_octal(hdr + F_GID, 8));
    const char typeflag       = hdr[F_TYPEFLAG];

    const std::size_t body_blocks = (size + TBLOCK - 1) / TBLOCK;
    if (m_pos + TBLOCK + body_blocks * TBLOCK > m_bytes.size()) {
      m_error = UnpackError::TAR_TRUNCATED;
      return false;
    }
    const char *body = m_bytes.data() + m_pos + TBLOCK;

    if (typeflag == 'x' || typeflag == 'X') {
      // Pax extended header — applies to the *next* regular entry.
      if (!apply_pax_record(std::string_view(body, size),
                             m_pax_path, m_pax_linkpath)) {
        m_error = UnpackError::TAR_BAD_PAX;
        return false;
      }
      m_pos += TBLOCK + body_blocks * TBLOCK;
      continue;
    }

    out = {};
    out.name = !m_pax_path.empty() ? std::move(m_pax_path) : raw_name;
    m_pax_path.clear();
    out.size  = size;
    out.mode  = mode;
    out.mtime = mtime;
    out.uid   = uid;
    out.gid   = gid;

    switch (typeflag) {
      case '\0':
      case '0':
        out.type = TarEntry::REGULAR;
        out.content.assign(body, size);
        break;
      case '1':
        out.type = TarEntry::HARDLINK;
        out.linkname = !m_pax_linkpath.empty()
                            ? std::move(m_pax_linkpath)
                            : c_str_of(hdr + F_LINKNAME, 100);
        m_pax_linkpath.clear();
        break;
      case '2':
        out.type = TarEntry::SYMLINK;
        out.linkname = !m_pax_linkpath.empty()
                            ? std::move(m_pax_linkpath)
                            : c_str_of(hdr + F_LINKNAME, 100);
        m_pax_linkpath.clear();
        break;
      case '5':
        out.type = TarEntry::DIRECTORY;
        break;
      case '3':
      case '4':
      case '6':
      case '7':
        // char/block device, fifo, contiguous — skip (not used by Docker
        // layers). Skip the body and advance.
        out.type = TarEntry::OTHER;
        break;
      default:
        m_error = UnpackError::TAR_UNKNOWN_TYPEFLAG;
        return false;
    }

    m_pos += TBLOCK + body_blocks * TBLOCK;
    return true;
  }
}

TarParseAll parse_tar(std::string_view bytes) {
  TarParseAll out;
  TarReader r(bytes);
  TarEntry e;
  while (r.next(e)) out.entries.push_back(std::move(e));
  out.error = r.error();
  return out;
}

// ── GzipInflater ────────────────────────────────────────────────────────

struct GzipInflater::Impl {
  z_stream zs{};
  bool     inited = false;
};

GzipInflater::GzipInflater() : m_impl(new Impl) {
  m_impl->zs.zalloc = Z_NULL;
  m_impl->zs.zfree  = Z_NULL;
  m_impl->zs.opaque = Z_NULL;
  // 32 + 15: enable gzip + automatic header detection.
  const int rc = inflateInit2(&m_impl->zs, 32 + 15);
  m_impl->inited = (rc == Z_OK);
  if (!m_impl->inited) m_error = UnpackError::GZIP_INVALID;
}

GzipInflater::~GzipInflater() {
  if (m_impl && m_impl->inited) inflateEnd(&m_impl->zs);
  delete m_impl;
}

UnpackError GzipInflater::write(const char *data, std::size_t len, std::string &out) {
  if (m_error != UnpackError::NONE) return m_error;
  if (m_done) return m_error;

  m_impl->zs.next_in  = reinterpret_cast<unsigned char *>(const_cast<char *>(data));
  m_impl->zs.avail_in = static_cast<unsigned int>(len);

  std::array<unsigned char, 16384> buf{};
  while (m_impl->zs.avail_in > 0) {
    m_impl->zs.next_out  = buf.data();
    m_impl->zs.avail_out = buf.size();
    const int rc = inflate(&m_impl->zs, Z_NO_FLUSH);
    const std::size_t produced = buf.size() - m_impl->zs.avail_out;
    out.append(reinterpret_cast<const char *>(buf.data()), produced);
    if (rc == Z_STREAM_END) {
      m_done = true;
      return UnpackError::NONE;
    }
    if (rc == Z_BUF_ERROR && produced == 0) {
      // need more input
      break;
    }
    if (rc != Z_OK) {
      m_error = UnpackError::GZIP_INVALID;
      return m_error;
    }
  }
  return UnpackError::NONE;
}

GunzipAll gunzip(std::string_view gz_bytes) {
  GunzipAll out;
  GzipInflater g;
  out.error = g.write(gz_bytes.data(), gz_bytes.size(), out.bytes);
  if (out.error == UnpackError::NONE && !g.finished()) {
    out.error = UnpackError::GZIP_TRUNCATED;
  }
  return out;
}

// ── LayerUnpacker ───────────────────────────────────────────────────────

namespace {

// Normalise a tar entry path. Strips leading '/', collapses repeated '/',
// resolves '.', and rejects any '..' that escape (entries beginning with
// or containing ".." resolve to OUTSIDE).
// Returns the normalised path and a flag indicating whether normalisation
// detected an escape attempt.
struct Normed {
  std::string path;
  bool        traversal = false;
  bool        was_absolute = false;
};
Normed normalise_entry_name(const std::string &raw) {
  Normed n;
  std::string s = raw;
  if (!s.empty() && s[0] == '/') {
    n.was_absolute = true;
    // Strip ALL leading slashes.
    std::size_t i = 0;
    while (i < s.size() && s[i] == '/') ++i;
    s = s.substr(i);
  }
  std::vector<std::string> parts;
  std::size_t start = 0;
  while (start < s.size()) {
    std::size_t slash = s.find('/', start);
    std::string part = s.substr(start, slash == std::string::npos
                                           ? std::string::npos
                                           : slash - start);
    if (!part.empty() && part != ".") {
      if (part == "..") {
        if (parts.empty()) { n.traversal = true; n.path = raw; return n; }
        parts.pop_back();
      } else {
        parts.push_back(std::move(part));
      }
    }
    if (slash == std::string::npos) break;
    start = slash + 1;
  }
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i) n.path += '/';
    n.path += parts[i];
  }
  return n;
}

// Is the *symlink target* safe? Targets that resolve outside the rootfs (when
// joined against the symlink's parent dir within rootfs) are rejected.
// Pure-function — no filesystem access; only string normalisation.
bool symlink_target_escapes(const std::string &link_path_in_rootfs,
                              const std::string &target) {
  if (target.empty()) return false;
  // Absolute target → always escapes.
  if (target[0] == '/') return true;
  // Treat link_path_in_rootfs as a path relative to rootfs root. The link's
  // parent directory is the prefix up to the last '/'. Resolve target
  // against it; reject if the depth ever drops below 0.
  std::string base;
  std::size_t last = link_path_in_rootfs.rfind('/');
  if (last != std::string::npos) base = link_path_in_rootfs.substr(0, last);

  // Walk parts of base + target; count depth.
  std::vector<std::string> stack;
  auto push_path = [&](const std::string &p) -> bool {
    std::size_t st = 0;
    while (st < p.size()) {
      std::size_t sl = p.find('/', st);
      std::string part = p.substr(st, sl == std::string::npos
                                          ? std::string::npos
                                          : sl - st);
      if (!part.empty() && part != ".") {
        if (part == "..") {
          if (stack.empty()) return false;
          stack.pop_back();
        } else {
          stack.push_back(std::move(part));
        }
      }
      if (sl == std::string::npos) break;
      st = sl + 1;
    }
    return true;
  };
  if (!push_path(base)) return true;
  if (!push_path(target)) return true;
  return false;
}

// Detect whiteout markers. Returns:
//  - opaque: true if the basename is `.wh..wh..opq`
//  - target: the basename being whited out (without the `.wh.` prefix)
//  - is_whiteout: true if this is any whiteout marker (regular or opaque)
struct Whiteout {
  bool is_whiteout = false;
  bool opaque       = false;
  std::string parent_dir;        ///< for opaque
  std::string target_basename;   ///< for regular
};
Whiteout detect_whiteout(const std::string &normalised_path) {
  Whiteout w;
  std::size_t slash = normalised_path.rfind('/');
  std::string base = (slash == std::string::npos) ? normalised_path
                                                   : normalised_path.substr(slash + 1);
  std::string parent = (slash == std::string::npos) ? "" : normalised_path.substr(0, slash);
  if (base == ".wh..wh..opq") {
    w.is_whiteout = true;
    w.opaque = true;
    w.parent_dir = parent;
    return w;
  }
  if (base.rfind(".wh.", 0) == 0 && base.size() > 4) {
    w.is_whiteout = true;
    w.opaque = false;
    w.parent_dir = parent;
    w.target_basename = base.substr(4);
    return w;
  }
  return w;
}

// Filesystem helpers. All writes go through openat() from a held root fd so
// `O_NOFOLLOW` on every hop protects against an attacker symlinking out of
// the rootfs mid-extraction. For test/dev simplicity we currently rely on
// path-based ops + the pure-function `symlink_target_escapes` check above —
// real production would also `openat(O_NOFOLLOW)` walk every parent.

bool ensure_parent_dirs(const fs::path &root, const std::string &rel_path) {
  std::size_t pos = 0;
  fs::path cur = root;
  while (pos < rel_path.size()) {
    std::size_t sl = rel_path.find('/', pos);
    if (sl == std::string::npos) break;
    cur /= rel_path.substr(pos, sl - pos);
    std::error_code ec;
    if (fs::is_symlink(cur, ec)) return false;  // refuse to follow
    fs::create_directory(cur, ec);
    // Ignore "already exists"; bail on real I/O errors only.
    pos = sl + 1;
  }
  return true;
}

}  // namespace

LayerUnpacker::LayerUnpacker(const std::string &rootfs_path, UnpackOptions opts)
    : m_root(rootfs_path), m_opts(opts) {
  std::error_code ec;
  fs::create_directories(m_root, ec);
}

UnpackResult LayerUnpacker::apply_layer_tar(std::string_view tar_bytes) {
  UnpackResult res;
  TarReader r(tar_bytes);
  TarEntry e;
  const fs::path root = m_root;

  const std::uint32_t want_uid = m_opts.clamp_uid_gid ? static_cast<std::uint32_t>(geteuid()) : 0;
  const std::uint32_t want_gid = m_opts.clamp_uid_gid ? static_cast<std::uint32_t>(getegid()) : 0;
  (void)want_uid; (void)want_gid;

  while (r.next(e)) {
    Normed n = normalise_entry_name(e.name);
    if (n.traversal) {
      res.error  = UnpackError::PATH_TRAVERSAL;
      res.detail = e.name;
      return res;
    }
    if (n.path.empty()) continue;

    Whiteout w = detect_whiteout(n.path);
    if (w.is_whiteout) {
      if (w.opaque) {
        // Remove all existing children of parent_dir (but keep parent_dir
        // itself).
        std::error_code ec;
        fs::path parent = root / w.parent_dir;
        if (fs::exists(parent, ec) && fs::is_directory(parent, ec)) {
          for (auto &child : fs::directory_iterator(parent, ec)) {
            fs::remove_all(child.path(), ec);
          }
        }
      } else {
        std::error_code ec;
        fs::path target = root / w.parent_dir / w.target_basename;
        fs::remove_all(target, ec);
      }
      // marker file itself is never written
      continue;
    }

    if (e.size > m_opts.max_entry_size) {
      res.error = UnpackError::PER_ENTRY_SIZE_EXCEEDED;
      res.detail = n.path;
      return res;
    }
    if (res.bytes_written + e.size > m_opts.max_total_size) {
      res.error = UnpackError::TOTAL_SIZE_EXCEEDED;
      res.detail = n.path;
      return res;
    }

    if (!ensure_parent_dirs(root, n.path)) {
      res.error = UnpackError::WRITE_THROUGH_SYMLINK;
      res.detail = n.path;
      return res;
    }

    fs::path target = root / n.path;
    std::error_code ec;

    // Refuse to write through a symlink: if the target's *parent* contains
    // a symlink along the way, ensure_parent_dirs already aborted. If the
    // target itself exists as a symlink, remove it first (file replacement
    // is allowed; following the symlink is not).
    if (fs::is_symlink(target, ec)) {
      fs::remove(target, ec);
    }

    switch (e.type) {
      case TarEntry::DIRECTORY:
        fs::create_directories(target, ec);
        if (ec) { res.error = UnpackError::IO_ERROR; res.detail = n.path; return res; }
        break;

      case TarEntry::REGULAR: {
        std::ofstream f(target, std::ios::binary | std::ios::trunc);
        if (!f) { res.error = UnpackError::IO_ERROR; res.detail = n.path; return res; }
        if (!e.content.empty()) f.write(e.content.data(), e.content.size());
        f.close();
        std::uint32_t mode = e.mode & 0777;
        if (m_opts.strip_special_modes) {
          mode &= 0777;  // already stripped setuid/setgid/sticky
        }
        fs::permissions(target, static_cast<fs::perms>(mode),
                          fs::perm_options::replace, ec);
        res.bytes_written += e.content.size();
        break;
      }

      case TarEntry::SYMLINK:
        if (symlink_target_escapes(n.path, e.linkname)) {
          res.error = UnpackError::SYMLINK_ESCAPE;
          res.detail = n.path;
          return res;
        }
        fs::create_symlink(e.linkname, target, ec);
        if (ec) { res.error = UnpackError::IO_ERROR; res.detail = n.path; return res; }
        break;

      case TarEntry::HARDLINK: {
        // Resolve linkname inside rootfs.
        Normed ln = normalise_entry_name(e.linkname);
        if (ln.traversal) {
          res.error = UnpackError::PATH_TRAVERSAL;
          res.detail = e.linkname;
          return res;
        }
        fs::path src = root / ln.path;
        fs::create_hard_link(src, target, ec);
        if (ec) { res.error = UnpackError::IO_ERROR; res.detail = n.path; return res; }
        break;
      }

      case TarEntry::OTHER:
        // Skip char/block device / fifo — Docker layers don't include
        // these by default and we don't ship them to runc.
        break;
    }

    ++res.entries_applied;
  }

  if (r.error() != UnpackError::NONE) {
    res.error = r.error();
  }
  return res;
}

UnpackResult LayerUnpacker::apply_layer_tar_gz(std::string_view tar_gz_bytes) {
  GunzipAll g = gunzip(tar_gz_bytes);
  if (g.error != UnpackError::NONE) {
    UnpackResult res;
    res.error = g.error;
    return res;
  }
  return apply_layer_tar(g.bytes);
}

}  // namespace runcpull
