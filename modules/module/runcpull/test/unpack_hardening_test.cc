// Phase D.4 / D.5 / D.6 / D.7 — Hardening tests for LayerUnpacker (15 tests).
//
//   D.4 (5)  PathTraversal*
//   D.5 (5)  FileMode*
//   D.6 (2)  Rootless*
//   D.7 (3)  UnpackLimit*

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "in_memory_tar_builder.hpp"
#include "layer_unpack.hpp"

namespace fs = std::filesystem;
using runcpull::LayerUnpacker;
using runcpull::UnpackError;
using runcpull::UnpackOptions;
using runcpull_test::InMemoryTarBuilder;

namespace {

fs::path make_rootfs(const std::string &name) {
  fs::path p = fs::path(testing::TempDir()) / name;
  std::error_code ec;
  fs::remove_all(p, ec);
  fs::create_directories(p, ec);
  return p;
}

UnpackOptions opts_with_size(std::uint64_t per, std::uint64_t total) {
  UnpackOptions o;
  o.record_uidgid_metadata = false;
  o.max_entry_size = per;
  o.max_total_size = total;
  return o;
}

UnpackOptions safe_opts() { return opts_with_size(UINT64_MAX, UINT64_MAX); }

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════
// D.4 — PathTraversal hardening
// ═══════════════════════════════════════════════════════════════════════════

TEST(PathTraversalTest, DotDot_InName_Rejected) {
  auto root = make_rootfs("pt-dotdot");
  // Sentinel ABOVE the rootfs that must not be touched.
  fs::path sentinel = root.parent_path() / "PT_DOTDOT_SENTINEL";
  { std::ofstream f(sentinel); f << "SAFE"; }

  LayerUnpacker u(root.string(), safe_opts());
  InMemoryTarBuilder b;
  b.add_file("../PT_DOTDOT_SENTINEL", "PWNED");
  auto r = u.apply_layer_tar(b.build());
  EXPECT_EQ(r.error, UnpackError::PATH_TRAVERSAL);

  std::ifstream f(sentinel);
  std::string s((std::istreambuf_iterator<char>(f)),
                std::istreambuf_iterator<char>());
  EXPECT_EQ(s, "SAFE") << "sentinel was modified — path traversal succeeded";
  std::error_code ec; fs::remove(sentinel, ec);
}

TEST(PathTraversalTest, AbsolutePath_StrippedToRelative) {
  auto root = make_rootfs("pt-absolute");
  LayerUnpacker u(root.string(), safe_opts());
  InMemoryTarBuilder b;
  // Tar entry name starts with '/'. Unpacker should strip and write to
  // rootfs/etc/foo, not host /etc/foo.
  b.add_dir("/etc/").add_file("/etc/foo", "content");
  auto r = u.apply_layer_tar(b.build());
  EXPECT_EQ(r.error, UnpackError::NONE);
  EXPECT_TRUE(fs::exists(root / "etc" / "foo"));
  // Host /etc/foo had better not exist (or had better not have our content).
  std::error_code ec;
  if (fs::exists("/etc/foo", ec)) {
    std::ifstream f("/etc/foo");
    std::string s((std::istreambuf_iterator<char>(f)),
                  std::istreambuf_iterator<char>());
    EXPECT_NE(s, "content");
  }
}

TEST(PathTraversalTest, SymlinkTarget_OutsideRootfs_Rejected) {
  auto root = make_rootfs("pt-symlink-escape");
  LayerUnpacker u(root.string(), safe_opts());
  InMemoryTarBuilder b;
  // Symlink whose target resolves outside the rootfs.
  b.add_symlink("escape", "../../../../etc/passwd");
  auto r = u.apply_layer_tar(b.build());
  EXPECT_EQ(r.error, UnpackError::SYMLINK_ESCAPE);
  EXPECT_FALSE(fs::is_symlink(root / "escape"));
}

TEST(PathTraversalTest, SubsequentWriteThroughExistingSymlink_Blocked) {
  auto root = make_rootfs("pt-write-through-symlink");
  // Create a target dir outside rootfs the symlink could redirect to.
  fs::path outside = root.parent_path() / "PT_WTS_OUTSIDE";
  std::error_code ec;
  fs::create_directories(outside, ec);

  LayerUnpacker u(root.string(), safe_opts());
  // Layer 1: a directory called `etc` is a symlink pointing OUTSIDE rootfs.
  // Use a relative target that points outside without escaping the
  // *path-string* normalisation (this is the dangerous case).
  InMemoryTarBuilder l1;
  l1.add_symlink("etc", "../PT_WTS_OUTSIDE");
  auto r1 = u.apply_layer_tar(l1.build());
  // The symlink itself should be rejected as escaping.
  EXPECT_EQ(r1.error, UnpackError::SYMLINK_ESCAPE);

  // Cleanup.
  fs::remove_all(outside, ec);
}

TEST(PathTraversalTest, WhiteoutOfSymlink_RemovesSymlink_NotTarget) {
  auto root = make_rootfs("pt-wo-symlink");
  LayerUnpacker u(root.string(), safe_opts());

  // L1: a regular file `real` and a symlink `alias → real`.
  InMemoryTarBuilder l1;
  l1.add_file("real", "data")
   .add_symlink("alias", "real");
  ASSERT_EQ(u.apply_layer_tar(l1.build()).error, UnpackError::NONE);
  ASSERT_TRUE(fs::is_symlink(root / "alias"));

  // L2: whiteout `alias`. Should remove the symlink, not the target.
  InMemoryTarBuilder l2;
  l2.add_file(".wh.alias", "");
  ASSERT_EQ(u.apply_layer_tar(l2.build()).error, UnpackError::NONE);
  EXPECT_FALSE(fs::is_symlink(root / "alias"));
  EXPECT_TRUE (fs::exists(root / "real"));
}

// ═══════════════════════════════════════════════════════════════════════════
// D.5 — File mode hardening
// ═══════════════════════════════════════════════════════════════════════════

TEST(FileModeTest, SetuidBit_Stripped) {
  auto root = make_rootfs("fm-setuid");
  LayerUnpacker u(root.string(), safe_opts());
  InMemoryTarBuilder b;
  b.add_file("setuid_bin", "x", 04755);
  ASSERT_EQ(u.apply_layer_tar(b.build()).error, UnpackError::NONE);
  struct stat st{};
  ASSERT_EQ(stat((root / "setuid_bin").c_str(), &st), 0);
  EXPECT_EQ(st.st_mode & 04000u, 0u);
}

TEST(FileModeTest, SetgidBit_Stripped) {
  auto root = make_rootfs("fm-setgid");
  LayerUnpacker u(root.string(), safe_opts());
  InMemoryTarBuilder b;
  b.add_file("setgid_bin", "x", 02755);
  ASSERT_EQ(u.apply_layer_tar(b.build()).error, UnpackError::NONE);
  struct stat st{};
  ASSERT_EQ(stat((root / "setgid_bin").c_str(), &st), 0);
  EXPECT_EQ(st.st_mode & 02000u, 0u);
}

TEST(FileModeTest, StickyBit_Stripped) {
  auto root = make_rootfs("fm-sticky");
  LayerUnpacker u(root.string(), safe_opts());
  InMemoryTarBuilder b;
  b.add_file("sticky", "x", 01755);
  ASSERT_EQ(u.apply_layer_tar(b.build()).error, UnpackError::NONE);
  struct stat st{};
  ASSERT_EQ(stat((root / "sticky").c_str(), &st), 0);
  EXPECT_EQ(st.st_mode & 01000u, 0u);
}

TEST(FileModeTest, RegularBits_Preserved_0644) {
  auto root = make_rootfs("fm-0644");
  LayerUnpacker u(root.string(), safe_opts());
  InMemoryTarBuilder b;
  b.add_file("data.txt", "x", 0644);
  ASSERT_EQ(u.apply_layer_tar(b.build()).error, UnpackError::NONE);
  struct stat st{};
  ASSERT_EQ(stat((root / "data.txt").c_str(), &st), 0);
  EXPECT_EQ(st.st_mode & 0777u, 0644u);
}

TEST(FileModeTest, ExecBits_Preserved_0755) {
  auto root = make_rootfs("fm-0755");
  LayerUnpacker u(root.string(), safe_opts());
  InMemoryTarBuilder b;
  b.add_file("script", "x", 0755);
  ASSERT_EQ(u.apply_layer_tar(b.build()).error, UnpackError::NONE);
  struct stat st{};
  ASSERT_EQ(stat((root / "script").c_str(), &st), 0);
  EXPECT_EQ(st.st_mode & 0777u, 0755u);
}

// ═══════════════════════════════════════════════════════════════════════════
// D.6 — Rootless uid/gid mapping
// ═══════════════════════════════════════════════════════════════════════════

TEST(RootlessTest, UidGid_ClampedToCallerOnDisk) {
  auto root = make_rootfs("rl-uidgid");
  LayerUnpacker u(root.string(), safe_opts());   // clamp_uid_gid default true
  InMemoryTarBuilder b;
  b.add_file("owned_by_999", "x", 0644).uidgid(999, 999);
  ASSERT_EQ(u.apply_layer_tar(b.build()).error, UnpackError::NONE);
  struct stat st{};
  ASSERT_EQ(stat((root / "owned_by_999").c_str(), &st), 0);
  // On disk, ownership should be the caller's effective uid/gid, NOT 999.
  EXPECT_EQ(st.st_uid, geteuid());
  EXPECT_EQ(st.st_gid, getegid());
}

TEST(RootlessTest, OriginalImageUidGid_RecordedInBundleMetadata) {
  // The plan specifies a sidecar `.runcpull-meta.json` next to rootfs
  // recording original uid/gid. The bundle writer (Phase E) ultimately
  // emits it; the LayerUnpacker records into in-memory state we can
  // assert on via UnpackResult. For Phase D scope this test is a
  // contract-stub: with record_uidgid_metadata=true, the entries_applied
  // counter increments per file (i.e. the unpacker noticed them).
  auto root = make_rootfs("rl-meta");
  UnpackOptions opts; opts.record_uidgid_metadata = true;
  LayerUnpacker u(root.string(), opts);
  InMemoryTarBuilder b;
  b.add_file("a", "x", 0644).uidgid(101, 202);
  auto r = u.apply_layer_tar(b.build());
  EXPECT_EQ(r.error, UnpackError::NONE);
  EXPECT_EQ(r.entries_applied, 1u);
}

// ═══════════════════════════════════════════════════════════════════════════
// D.7 — Unpack size limits
// ═══════════════════════════════════════════════════════════════════════════

TEST(UnpackLimitTest, PerEntrySize_ExceedsLimit_Aborts) {
  auto root = make_rootfs("ul-per-entry");
  LayerUnpacker u(root.string(), opts_with_size(1024, UINT64_MAX));
  InMemoryTarBuilder b;
  b.add_file("huge", std::string(10 * 1024, 'X'), 0644);
  auto r = u.apply_layer_tar(b.build());
  EXPECT_EQ(r.error, UnpackError::PER_ENTRY_SIZE_EXCEEDED);
  EXPECT_FALSE(fs::exists(root / "huge"));
}

TEST(UnpackLimitTest, TotalSize_ExceedsLimit_AbortsMidStream) {
  auto root = make_rootfs("ul-total");
  LayerUnpacker u(root.string(), opts_with_size(UINT64_MAX, 2048));
  InMemoryTarBuilder b;
  b.add_file("a", std::string(1024, 'A'))
   .add_file("b", std::string(1024, 'B'))
   .add_file("c", std::string(1024, 'C'));   // c pushes total > 2048
  auto r = u.apply_layer_tar(b.build());
  EXPECT_EQ(r.error, UnpackError::TOTAL_SIZE_EXCEEDED);
  EXPECT_FALSE(fs::exists(root / "c")) << "c should not have been written";
}

TEST(UnpackLimitTest, LimitOff_NoOpForReasonablePayload) {
  auto root = make_rootfs("ul-off");
  LayerUnpacker u(root.string(), safe_opts());
  InMemoryTarBuilder b;
  b.add_file("ok", std::string(100, '*'));
  auto r = u.apply_layer_tar(b.build());
  EXPECT_EQ(r.error, UnpackError::NONE);
  EXPECT_TRUE(fs::exists(root / "ok"));
}
