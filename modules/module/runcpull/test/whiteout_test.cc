// Phase D.3 — OCI whiteout semantics under LayerUnpacker (8 tests).

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

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

UnpackOptions safe_opts() {
  UnpackOptions o;
  o.record_uidgid_metadata = false;
  return o;
}

}  // namespace

TEST(WhiteoutTest, RegularWhiteout_DeletesPriorFile) {
  auto root = make_rootfs("wo-regular-delete");
  LayerUnpacker u(root.string(), safe_opts());

  InMemoryTarBuilder l1;
  l1.add_dir("etc/").add_file("etc/hosts", "127.0.0.1\n");
  ASSERT_EQ(u.apply_layer_tar(l1.build()).error, UnpackError::NONE);
  ASSERT_TRUE(fs::exists(root / "etc/hosts"));

  InMemoryTarBuilder l2;
  l2.add_file("etc/.wh.hosts", "");
  ASSERT_EQ(u.apply_layer_tar(l2.build()).error, UnpackError::NONE);
  EXPECT_FALSE(fs::exists(root / "etc/hosts"));
}

TEST(WhiteoutTest, RegularWhiteout_FileNotWrittenToRootfs) {
  auto root = make_rootfs("wo-marker-not-written");
  LayerUnpacker u(root.string(), safe_opts());

  InMemoryTarBuilder l1;
  l1.add_dir("etc/").add_file("etc/hosts", "127.0.0.1\n");
  u.apply_layer_tar(l1.build());

  InMemoryTarBuilder l2;
  l2.add_file("etc/.wh.hosts", "");
  u.apply_layer_tar(l2.build());

  EXPECT_FALSE(fs::exists(root / "etc/.wh.hosts"));
}

TEST(WhiteoutTest, RegularWhiteout_LeavesSiblingFiles) {
  auto root = make_rootfs("wo-leaves-siblings");
  LayerUnpacker u(root.string(), safe_opts());

  InMemoryTarBuilder l1;
  l1.add_dir("etc/")
   .add_file("etc/hosts",  "127.0.0.1\n")
   .add_file("etc/passwd", "root:x:0:0\n");
  u.apply_layer_tar(l1.build());

  InMemoryTarBuilder l2;
  l2.add_file("etc/.wh.hosts", "");
  u.apply_layer_tar(l2.build());

  EXPECT_FALSE(fs::exists(root / "etc/hosts"));
  EXPECT_TRUE (fs::exists(root / "etc/passwd"));
}

TEST(WhiteoutTest, OpaqueWhiteout_ClearsParentDirContents) {
  auto root = make_rootfs("wo-opaque");
  LayerUnpacker u(root.string(), safe_opts());

  InMemoryTarBuilder l1;
  l1.add_dir("etc/")
   .add_file("etc/hosts",  "old\n")
   .add_file("etc/passwd", "old\n");
  u.apply_layer_tar(l1.build());

  InMemoryTarBuilder l2;
  l2.add_file("etc/.wh..wh..opq", "");
  u.apply_layer_tar(l2.build());

  EXPECT_TRUE (fs::exists(root / "etc"));
  EXPECT_FALSE(fs::exists(root / "etc/hosts"));
  EXPECT_FALSE(fs::exists(root / "etc/passwd"));
}

TEST(WhiteoutTest, OpaqueWhiteout_NewEntriesInSameLayer_Survive) {
  auto root = make_rootfs("wo-opaque-newentries");
  LayerUnpacker u(root.string(), safe_opts());

  InMemoryTarBuilder l1;
  l1.add_dir("etc/").add_file("etc/old.conf", "X");
  u.apply_layer_tar(l1.build());

  InMemoryTarBuilder l2;
  l2.add_file("etc/.wh..wh..opq", "")
   .add_file("etc/new.conf", "Y");
  u.apply_layer_tar(l2.build());

  EXPECT_FALSE(fs::exists(root / "etc/old.conf"));
  EXPECT_TRUE (fs::exists(root / "etc/new.conf"));
}

TEST(WhiteoutTest, OpaqueWhiteout_LeavesParentDirItself) {
  auto root = make_rootfs("wo-opaque-parent-survives");
  LayerUnpacker u(root.string(), safe_opts());

  InMemoryTarBuilder l1;
  l1.add_dir("etc/").add_file("etc/hosts", "X");
  u.apply_layer_tar(l1.build());

  InMemoryTarBuilder l2;
  l2.add_file("etc/.wh..wh..opq", "");
  u.apply_layer_tar(l2.build());

  EXPECT_TRUE(fs::exists(root / "etc"));
  EXPECT_TRUE(fs::is_directory(root / "etc"));
}

TEST(WhiteoutTest, OpaqueWhiteout_LeavesUnrelatedDirs) {
  auto root = make_rootfs("wo-opaque-unrelated");
  LayerUnpacker u(root.string(), safe_opts());

  InMemoryTarBuilder l1;
  l1.add_dir("etc/").add_file("etc/hosts", "X")
   .add_dir("var/").add_file("var/log", "Y");
  u.apply_layer_tar(l1.build());

  InMemoryTarBuilder l2;
  l2.add_file("etc/.wh..wh..opq", "");
  u.apply_layer_tar(l2.build());

  EXPECT_FALSE(fs::exists(root / "etc/hosts"));
  EXPECT_TRUE (fs::exists(root / "var/log"));
}

TEST(WhiteoutTest, FileResurrectedInLaterLayer_FinalStateLatest) {
  auto root = make_rootfs("wo-resurrected");
  LayerUnpacker u(root.string(), safe_opts());

  // L1: file with content A.
  InMemoryTarBuilder l1;
  l1.add_dir("etc/").add_file("etc/hosts", "A\n");
  u.apply_layer_tar(l1.build());

  // L2: whiteout removes it.
  InMemoryTarBuilder l2;
  l2.add_file("etc/.wh.hosts", "");
  u.apply_layer_tar(l2.build());

  // L3: re-creates with content B.
  InMemoryTarBuilder l3;
  l3.add_file("etc/hosts", "B\n");
  u.apply_layer_tar(l3.build());

  ASSERT_TRUE(fs::exists(root / "etc/hosts"));
  std::ifstream f(root / "etc/hosts");
  std::string content((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());
  EXPECT_EQ(content, "B\n");
}
