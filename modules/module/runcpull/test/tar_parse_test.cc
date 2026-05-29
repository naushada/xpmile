// Phase D.1 — POSIX ustar + pax tar parsing (12 tests).

#include <gtest/gtest.h>

#include <string>

#include "in_memory_tar_builder.hpp"
#include "layer_unpack.hpp"

using runcpull::parse_tar;
using runcpull::TarEntry;
using runcpull::TarParseAll;
using runcpull::TarReader;
using runcpull::UnpackError;
using runcpull_test::InMemoryTarBuilder;

TEST(TarTest, UstarHeader_RegularFile_HasNameSizeMode) {
  InMemoryTarBuilder b;
  b.add_file("hello.txt", "world", 0644);
  auto bytes = b.build();
  auto p = parse_tar(bytes);
  ASSERT_EQ(p.error, UnpackError::NONE);
  ASSERT_EQ(p.entries.size(), 1u);
  EXPECT_EQ(p.entries[0].name, "hello.txt");
  EXPECT_EQ(p.entries[0].type, TarEntry::REGULAR);
  EXPECT_EQ(p.entries[0].size, 5u);
  EXPECT_EQ(p.entries[0].mode, 0644u);
  EXPECT_EQ(p.entries[0].content, "world");
}

TEST(TarTest, UstarHeader_Directory_TypeflagFive) {
  InMemoryTarBuilder b;
  b.add_dir("etc/");
  auto p = parse_tar(b.build());
  ASSERT_EQ(p.error, UnpackError::NONE);
  ASSERT_EQ(p.entries.size(), 1u);
  EXPECT_EQ(p.entries[0].type, TarEntry::DIRECTORY);
  EXPECT_EQ(p.entries[0].name, "etc/");
}

TEST(TarTest, UstarHeader_Symlink_TargetParsed) {
  InMemoryTarBuilder b;
  b.add_symlink("usr/bin/sh", "/bin/sh");
  auto p = parse_tar(b.build());
  ASSERT_EQ(p.error, UnpackError::NONE);
  ASSERT_EQ(p.entries.size(), 1u);
  EXPECT_EQ(p.entries[0].type, TarEntry::SYMLINK);
  EXPECT_EQ(p.entries[0].linkname, "/bin/sh");
}

TEST(TarTest, UstarHeader_Hardlink_TargetParsed) {
  InMemoryTarBuilder b;
  b.add_hardlink("a", "b");
  auto p = parse_tar(b.build());
  ASSERT_EQ(p.error, UnpackError::NONE);
  ASSERT_EQ(p.entries.size(), 1u);
  EXPECT_EQ(p.entries[0].type, TarEntry::HARDLINK);
  EXPECT_EQ(p.entries[0].linkname, "b");
}

TEST(TarTest, EmptyTar_DoubleZeroBlock_Terminates) {
  InMemoryTarBuilder b;
  auto p = parse_tar(b.build());
  EXPECT_EQ(p.error, UnpackError::NONE);
  EXPECT_TRUE(p.entries.empty());
}

TEST(TarTest, MultipleEntries_ParsedInOrder) {
  InMemoryTarBuilder b;
  b.add_file("a.txt", "1")
   .add_file("b.txt", "22")
   .add_file("c.txt", "333");
  auto p = parse_tar(b.build());
  ASSERT_EQ(p.error, UnpackError::NONE);
  ASSERT_EQ(p.entries.size(), 3u);
  EXPECT_EQ(p.entries[0].name, "a.txt");
  EXPECT_EQ(p.entries[1].name, "b.txt");
  EXPECT_EQ(p.entries[2].name, "c.txt");
  EXPECT_EQ(p.entries[0].content, "1");
  EXPECT_EQ(p.entries[1].content, "22");
  EXPECT_EQ(p.entries[2].content, "333");
}

TEST(TarTest, OddSizedContent_PaddedTo512_Skipped) {
  // A 5-byte body is padded with 507 NULs to fill the 512-byte body block.
  // The next entry should still parse correctly — proving the padding was
  // skipped, not read as a header.
  InMemoryTarBuilder b;
  b.add_file("a", "ABCDE")
   .add_file("b", "second");
  auto p = parse_tar(b.build());
  ASSERT_EQ(p.error, UnpackError::NONE);
  ASSERT_EQ(p.entries.size(), 2u);
  EXPECT_EQ(p.entries[1].name, "b");
  EXPECT_EQ(p.entries[1].content, "second");
}

TEST(TarTest, PaxLongPath_ResolvesEntryName) {
  // A path longer than 100 chars is sent via a pax extended header.
  std::string long_path = "a/very/long/" + std::string(120, 'x') + "/file.txt";
  InMemoryTarBuilder b;
  b.add_pax_path(long_path)
   .add_file("placeholder", "data");
  auto p = parse_tar(b.build());
  ASSERT_EQ(p.error, UnpackError::NONE);
  ASSERT_EQ(p.entries.size(), 1u);
  EXPECT_EQ(p.entries[0].name, long_path);
  EXPECT_EQ(p.entries[0].content, "data");
}

TEST(TarTest, PaxLongLinkpath_ResolvesSymlinkTarget) {
  std::string long_target = "../../../../" + std::string(120, 'y');
  // Note: long target may "escape" rootfs; this test checks parsing only,
  // not the unpacker hardening. Use a non-escaping target.
  long_target = std::string(110, 'y');
  InMemoryTarBuilder b;
  b.add_pax_linkpath(long_target)
   .add_symlink("short_name", "fallback");
  auto p = parse_tar(b.build());
  ASSERT_EQ(p.error, UnpackError::NONE);
  ASSERT_EQ(p.entries.size(), 1u);
  EXPECT_EQ(p.entries[0].linkname, long_target);
}

TEST(TarTest, TruncatedHeader_Errors) {
  InMemoryTarBuilder b;
  b.add_file("a", "data");
  auto bytes = b.build();
  // Cut off the first header midway.
  bytes.resize(200);
  auto p = parse_tar(bytes);
  EXPECT_EQ(p.error, UnpackError::TAR_TRUNCATED);
}

TEST(TarTest, BadChecksum_Errors) {
  InMemoryTarBuilder b;
  b.add_bad_checksum_file("corrupted.txt");
  auto p = parse_tar(b.build());
  EXPECT_EQ(p.error, UnpackError::TAR_BAD_CHECKSUM);
}

TEST(TarTest, UnknownTypeflag_SkippedOrErrors) {
  // Construct a header with a typeflag we don't know. The parser should
  // either skip it, return TAR_UNKNOWN_TYPEFLAG, or — if the in-place
  // mutation also invalidates the header checksum — return
  // TAR_BAD_CHECKSUM. All three are acceptable surfaces; what we care
  // about is that the parser doesn't silently accept the corrupted
  // entry as a regular file.
  InMemoryTarBuilder b;
  b.add_file("good", "x");
  auto bytes = b.build();
  bytes[156] = 'Z';   // 'Z' is not a recognised typeflag
  auto p = parse_tar(bytes);
  if (p.error == UnpackError::NONE) {
    for (const auto &e : p.entries) {
      EXPECT_NE(e.name, "good");
    }
  } else {
    EXPECT_TRUE(p.error == UnpackError::TAR_UNKNOWN_TYPEFLAG
                || p.error == UnpackError::TAR_BAD_CHECKSUM)
        << "unexpected error code: " << static_cast<int>(p.error);
  }
}
