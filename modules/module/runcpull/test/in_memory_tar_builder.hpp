// Test-only helper: build a POSIX ustar tar archive in memory by appending
// directory / regular / symlink / hardlink / pax entries. Used by all four
// Phase D test files so a tar fixture can be constructed inline instead of
// checked into git as a binary blob.
//
// Header-only so multiple test translation units pick it up without a
// separate .cpp source.

#ifndef RUNCPULL_TEST_IN_MEMORY_TAR_BUILDER_HPP
#define RUNCPULL_TEST_IN_MEMORY_TAR_BUILDER_HPP

#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace runcpull_test {

class InMemoryTarBuilder {
public:
  struct Entry {
    enum Type { DIR, FILE, SYMLINK, HARDLINK, PAX };
    Type        type = FILE;
    std::string name;
    std::string content;
    std::string linkname;
    std::uint32_t mode  = 0644;
    std::uint64_t mtime = 0;
    std::uint32_t uid   = 0;
    std::uint32_t gid   = 0;
    bool          bad_checksum = false;  ///< testing hook
  };

  InMemoryTarBuilder &add_dir(const std::string &name,
                                std::uint32_t mode = 0755) {
    Entry e; e.type = Entry::DIR; e.name = name; e.mode = mode;
    m_entries.push_back(std::move(e));
    return *this;
  }
  InMemoryTarBuilder &add_file(const std::string &name,
                                 std::string content,
                                 std::uint32_t mode = 0644) {
    Entry e; e.type = Entry::FILE; e.name = name;
    e.content = std::move(content); e.mode = mode;
    m_entries.push_back(std::move(e));
    return *this;
  }
  InMemoryTarBuilder &add_symlink(const std::string &name,
                                    const std::string &target) {
    Entry e; e.type = Entry::SYMLINK; e.name = name;
    e.linkname = target; e.mode = 0777;
    m_entries.push_back(std::move(e));
    return *this;
  }
  InMemoryTarBuilder &add_hardlink(const std::string &name,
                                     const std::string &target) {
    Entry e; e.type = Entry::HARDLINK; e.name = name;
    e.linkname = target; e.mode = 0644;
    m_entries.push_back(std::move(e));
    return *this;
  }
  // Pax extended header carrying a path= override for the next entry.
  // The next call to add_file/add_dir is the one that the pax header
  // applies to.
  InMemoryTarBuilder &add_pax_path(const std::string &long_path) {
    Entry e; e.type = Entry::PAX; e.name = long_path;
    m_entries.push_back(std::move(e));
    return *this;
  }
  InMemoryTarBuilder &add_pax_linkpath(const std::string &long_path) {
    Entry e; e.type = Entry::PAX; e.name = "";
    e.linkname = long_path;
    m_entries.push_back(std::move(e));
    return *this;
  }
  // Add a regular entry with deliberately invalid checksum for parser tests.
  InMemoryTarBuilder &add_bad_checksum_file(const std::string &name) {
    Entry e; e.type = Entry::FILE; e.name = name; e.bad_checksum = true;
    m_entries.push_back(std::move(e));
    return *this;
  }
  InMemoryTarBuilder &mode(std::uint32_t m) {
    if (!m_entries.empty()) m_entries.back().mode = m;
    return *this;
  }
  InMemoryTarBuilder &uidgid(std::uint32_t u, std::uint32_t g) {
    if (!m_entries.empty()) { m_entries.back().uid = u; m_entries.back().gid = g; }
    return *this;
  }

  std::string build() const {
    std::string out;
    std::string pending_pax_path;
    std::string pending_pax_linkpath;
    for (const auto &e : m_entries) {
      if (e.type == Entry::PAX) {
        if (!e.name.empty()) {
          append_pax_block(out, "path", e.name);
          pending_pax_path = e.name;
        }
        if (!e.linkname.empty()) {
          append_pax_block(out, "linkpath", e.linkname);
          pending_pax_linkpath = e.linkname;
        }
        continue;
      }
      append_ustar(out, e);
      pending_pax_path.clear();
      pending_pax_linkpath.clear();
    }
    // End-of-archive: two zero blocks.
    out.append(1024, '\0');
    return out;
  }

private:
  std::vector<Entry> m_entries;

  static void put_octal(char *dst, std::size_t width, std::uint64_t v) {
    // POSIX ustar octal: right-aligned, NUL-terminated; we use the simpler
    // form right-aligned with leading zeros + space + '\0' for width≥2.
    std::string s;
    if (v == 0) s = "0";
    else { while (v) { s.insert(s.begin(), '0' + (v & 7)); v >>= 3; } }
    if (s.size() >= width) s = s.substr(s.size() - (width - 1));
    while (s.size() < width - 1) s.insert(s.begin(), '0');
    std::memcpy(dst, s.data(), width - 1);
    dst[width - 1] = '\0';
  }

  static void append_ustar(std::string &out, const Entry &e) {
    char hdr[512];
    std::memset(hdr, 0, sizeof(hdr));

    // Name (max 100 ustar; longer is split via prefix or via pax — we keep
    // it simple and assume ≤100 for the convenience helpers).
    std::strncpy(hdr + 0, e.name.c_str(), 100);

    put_octal(hdr + 100, 8, e.mode);
    put_octal(hdr + 108, 8, e.uid);
    put_octal(hdr + 116, 8, e.gid);
    std::uint64_t size = (e.type == Entry::FILE) ? e.content.size() : 0;
    put_octal(hdr + 124, 12, size);
    put_octal(hdr + 136, 12, e.mtime);

    char typeflag;
    switch (e.type) {
      case Entry::DIR:      typeflag = '5'; break;
      case Entry::SYMLINK:  typeflag = '2'; break;
      case Entry::HARDLINK: typeflag = '1'; break;
      case Entry::FILE:     typeflag = '0'; break;
      default:              typeflag = '0'; break;
    }
    hdr[156] = typeflag;
    if (!e.linkname.empty()) {
      std::strncpy(hdr + 157, e.linkname.c_str(), 100);
    }
    std::memcpy(hdr + 257, "ustar\0", 6);   // POSIX magic
    std::memcpy(hdr + 263, "00", 2);          // version

    // Checksum: fill field with spaces, sum bytes, write octal.
    std::memset(hdr + 148, ' ', 8);
    std::uint32_t sum = 0;
    for (std::size_t i = 0; i < 512; ++i) sum += static_cast<std::uint8_t>(hdr[i]);
    put_octal(hdr + 148, 8, sum);
    if (e.bad_checksum) hdr[148] = '?';

    out.append(hdr, 512);

    if (e.type == Entry::FILE && !e.content.empty()) {
      out.append(e.content);
      const std::size_t pad = (512 - (e.content.size() % 512)) % 512;
      if (pad) out.append(pad, '\0');
    }
  }

  static void append_pax_block(std::string &out, const std::string &key, const std::string &val) {
    // pax record: "<len> key=val\n". The <len> includes itself + space +
    // key + '=' + val + '\n'. Compute iteratively.
    std::string rec_body = " " + key + "=" + val + "\n";
    std::size_t len = rec_body.size();
    std::string len_s = std::to_string(len);
    while (true) {
      std::size_t tot = len_s.size() + rec_body.size();
      if (std::to_string(tot).size() == len_s.size()) {
        len_s = std::to_string(tot);
        break;
      }
      len_s = std::to_string(tot);
    }
    std::string rec = len_s + rec_body;

    Entry pax_hdr;
    pax_hdr.type = Entry::FILE;
    pax_hdr.name = "PaxHeader";
    pax_hdr.content = rec;
    // Hand-build with typeflag 'x' (pax extended header).
    char hdr[512];
    std::memset(hdr, 0, sizeof(hdr));
    std::strncpy(hdr + 0, "PaxHeader", 100);
    put_octal(hdr + 100, 8, 0644);
    put_octal(hdr + 108, 8, 0);
    put_octal(hdr + 116, 8, 0);
    put_octal(hdr + 124, 12, rec.size());
    put_octal(hdr + 136, 12, 0);
    hdr[156] = 'x';
    std::memcpy(hdr + 257, "ustar\0", 6);
    std::memcpy(hdr + 263, "00", 2);
    std::memset(hdr + 148, ' ', 8);
    std::uint32_t sum = 0;
    for (std::size_t i = 0; i < 512; ++i) sum += static_cast<std::uint8_t>(hdr[i]);
    put_octal(hdr + 148, 8, sum);
    out.append(hdr, 512);
    out.append(rec);
    const std::size_t pad = (512 - (rec.size() % 512)) % 512;
    if (pad) out.append(pad, '\0');
  }
};

}  // namespace runcpull_test

#endif  // RUNCPULL_TEST_IN_MEMORY_TAR_BUILDER_HPP
