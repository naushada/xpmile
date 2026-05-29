// Phase C, Steps C.1 + C.2 — manifest / image-index parsing.
// 12 tests with the `Manifest*` prefix.

#include "manifest.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

using runcpull::Manifest;
using runcpull::ManifestList;
using runcpull::parse_manifest;
using runcpull::manifest_pick_platform;

std::string fixture_path(const char *name) {
  std::filesystem::path here(__FILE__);
  return (here.parent_path() / "fixtures" / name).string();
}

std::string read_fixture(const char *name) {
  std::ifstream f(fixture_path(name));
  if (!f) {
    ADD_FAILURE() << "could not read fixture: " << fixture_path(name);
    return {};
  }
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

// ── C.1: manifest list / image index ──────────────────────────────────────

TEST(ManifestTest, OciImageIndex_ParsesAllPlatforms) {
  auto m = parse_manifest(read_fixture("oci_image_index.json"));
  ASSERT_TRUE(m.has_value());
  ASSERT_TRUE(m->is_list());
  ASSERT_TRUE(m->list.has_value());

  const auto &list = *m->list;
  EXPECT_EQ(list.media_type, "application/vnd.oci.image.index.v1+json");
  ASSERT_EQ(list.entries.size(), 4u);

  // Entry 0: linux/amd64.
  EXPECT_EQ(list.entries[0].os, "linux");
  EXPECT_EQ(list.entries[0].architecture, "amd64");
  EXPECT_EQ(list.entries[0].variant, "");
  EXPECT_EQ(
      list.entries[0].digest,
      "sha256:1111111111111111111111111111111111111111111111111111111111111111");
  EXPECT_EQ(list.entries[0].media_type,
            "application/vnd.oci.image.manifest.v1+json");
  EXPECT_EQ(list.entries[0].size, 525);

  // Entry 1: linux/arm64 v8.
  EXPECT_EQ(list.entries[1].os, "linux");
  EXPECT_EQ(list.entries[1].architecture, "arm64");
  EXPECT_EQ(list.entries[1].variant, "v8");

  // Entry 2: linux/arm v7.
  EXPECT_EQ(list.entries[2].os, "linux");
  EXPECT_EQ(list.entries[2].architecture, "arm");
  EXPECT_EQ(list.entries[2].variant, "v7");

  // Entry 3: windows/amd64.
  EXPECT_EQ(list.entries[3].os, "windows");
  EXPECT_EQ(list.entries[3].architecture, "amd64");
}

TEST(ManifestTest, OciImageIndex_PicksLinuxArm64) {
  auto m = parse_manifest(read_fixture("oci_image_index.json"));
  ASSERT_TRUE(m.has_value() && m->is_list());

  auto idx = manifest_pick_platform(*m->list, "linux", "arm64", "v8");
  ASSERT_TRUE(idx.has_value());
  EXPECT_EQ(*idx, 1u);

  // Without a variant hint, the same arm64 entry is still selected
  // (only one arm64 entry in this fixture).
  auto idx2 = manifest_pick_platform(*m->list, "linux", "arm64", "");
  ASSERT_TRUE(idx2.has_value());
  EXPECT_EQ(*idx2, 1u);
}

TEST(ManifestTest, OciImageIndex_PicksLinuxArmV7_ByVariant) {
  auto m = parse_manifest(read_fixture("oci_image_index.json"));
  ASSERT_TRUE(m.has_value() && m->is_list());

  auto idx = manifest_pick_platform(*m->list, "linux", "arm", "v7");
  ASSERT_TRUE(idx.has_value());
  EXPECT_EQ(*idx, 2u);
}

TEST(ManifestTest, OciImageIndex_NoMatch_Errors) {
  auto m = parse_manifest(read_fixture("oci_image_index.json"));
  ASSERT_TRUE(m.has_value() && m->is_list());

  // No darwin entries.
  EXPECT_FALSE(manifest_pick_platform(*m->list, "darwin", "amd64", "").has_value());
  // No risc-v entries.
  EXPECT_FALSE(manifest_pick_platform(*m->list, "linux", "riscv64", "").has_value());
}

TEST(ManifestTest, OciImageIndex_PrefersExactVariant_OverNone) {
  // Two arm64 entries — one with variant "v8", one with no variant.
  // Host says linux/arm64 (no variant) → the no-variant entry wins.
  // Host says linux/arm64/v8 → the v8 entry wins.
  const std::string body = R"({
    "schemaVersion": 2,
    "mediaType": "application/vnd.oci.image.index.v1+json",
    "manifests": [
      {
        "mediaType": "application/vnd.oci.image.manifest.v1+json",
        "digest": "sha256:1111111111111111111111111111111111111111111111111111111111111111",
        "size": 525,
        "platform": {"os": "linux", "architecture": "arm64", "variant": "v8"}
      },
      {
        "mediaType": "application/vnd.oci.image.manifest.v1+json",
        "digest": "sha256:2222222222222222222222222222222222222222222222222222222222222222",
        "size": 525,
        "platform": {"os": "linux", "architecture": "arm64"}
      }
    ]
  })";

  auto m = parse_manifest(body);
  ASSERT_TRUE(m.has_value() && m->is_list());

  // No variant requested → prefer the entry with no variant (index 1).
  auto no_var = manifest_pick_platform(*m->list, "linux", "arm64", "");
  ASSERT_TRUE(no_var.has_value());
  EXPECT_EQ(*no_var, 1u);

  // Variant v8 requested → prefer the exact-variant entry (index 0).
  auto v8 = manifest_pick_platform(*m->list, "linux", "arm64", "v8");
  ASSERT_TRUE(v8.has_value());
  EXPECT_EQ(*v8, 0u);
}

TEST(ManifestTest, DockerManifestList_ParsesAndSelects) {
  auto m = parse_manifest(read_fixture("docker_manifest_list.json"));
  ASSERT_TRUE(m.has_value());
  ASSERT_TRUE(m->is_list());

  const auto &list = *m->list;
  EXPECT_EQ(list.media_type,
            "application/vnd.docker.distribution.manifest.list.v2+json");
  ASSERT_EQ(list.entries.size(), 2u);

  auto idx = manifest_pick_platform(list, "linux", "arm64", "");
  ASSERT_TRUE(idx.has_value());
  EXPECT_EQ(*idx, 1u);
  EXPECT_EQ(
      list.entries[*idx].digest,
      "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
}

TEST(ManifestTest, MediaType_UnknownIndex_Errors) {
  // mediaType set to something the puller doesn't speak — must be
  // rejected at parse time so we don't silently misinterpret an unknown
  // shape.
  const std::string body = R"({
    "schemaVersion": 2,
    "mediaType": "application/vnd.example.unknown+json",
    "manifests": []
  })";
  EXPECT_FALSE(parse_manifest(body).has_value());

  // Missing mediaType altogether.
  const std::string no_mt = R"({"schemaVersion": 2, "manifests": []})";
  EXPECT_FALSE(parse_manifest(no_mt).has_value());

  // Empty body.
  EXPECT_FALSE(parse_manifest("").has_value());

  // Malformed JSON.
  EXPECT_FALSE(parse_manifest("{ not json").has_value());
}

// ── C.2: single-arch image manifest ───────────────────────────────────────

TEST(ManifestTest, OciImageManifest_HasConfigAndLayers) {
  auto m = parse_manifest(read_fixture("oci_image_manifest.json"));
  ASSERT_TRUE(m.has_value());
  ASSERT_TRUE(m->is_image());
  ASSERT_TRUE(m->image.has_value());

  const auto &img = *m->image;
  EXPECT_EQ(img.media_type, "application/vnd.oci.image.manifest.v1+json");

  EXPECT_EQ(img.config.media_type, "application/vnd.oci.image.config.v1+json");
  EXPECT_EQ(
      img.config.digest,
      "sha256:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc");
  EXPECT_EQ(img.config.size, 1471);

  EXPECT_EQ(img.layers.size(), 3u);
}

TEST(ManifestTest, OciImageManifest_LayersInArrayOrder) {
  auto m = parse_manifest(read_fixture("oci_image_manifest.json"));
  ASSERT_TRUE(m.has_value() && m->is_image());

  const auto &layers = m->image->layers;
  ASSERT_EQ(layers.size(), 3u);
  EXPECT_EQ(
      layers[0].digest,
      "sha256:1010101010101010101010101010101010101010101010101010101010101010");
  EXPECT_EQ(
      layers[1].digest,
      "sha256:2020202020202020202020202020202020202020202020202020202020202020");
  EXPECT_EQ(
      layers[2].digest,
      "sha256:3030303030303030303030303030303030303030303030303030303030303030");

  EXPECT_EQ(layers[0].size, 3370057);
  EXPECT_EQ(layers[1].size, 1024);
  EXPECT_EQ(layers[2].size, 2048);
}

TEST(ManifestTest, DockerManifestV2_HasConfigAndLayers) {
  auto m = parse_manifest(read_fixture("docker_manifest_v2.json"));
  ASSERT_TRUE(m.has_value());
  ASSERT_TRUE(m->is_image());

  const auto &img = *m->image;
  EXPECT_EQ(img.media_type,
            "application/vnd.docker.distribution.manifest.v2+json");
  EXPECT_EQ(
      img.config.digest,
      "sha256:dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd");
  ASSERT_EQ(img.layers.size(), 2u);
  EXPECT_EQ(
      img.layers[0].digest,
      "sha256:4040404040404040404040404040404040404040404040404040404040404040");
  EXPECT_EQ(
      img.layers[1].digest,
      "sha256:5050505050505050505050505050505050505050505050505050505050505050");
}

TEST(ManifestTest, ImageManifest_MissingLayers_Errors) {
  // Image manifest with `config` but no `layers` — invalid per spec.
  const std::string no_layers = R"({
    "schemaVersion": 2,
    "mediaType": "application/vnd.oci.image.manifest.v1+json",
    "config": {
      "mediaType": "application/vnd.oci.image.config.v1+json",
      "digest": "sha256:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc",
      "size": 1471
    }
  })";
  EXPECT_FALSE(parse_manifest(no_layers).has_value());

  // Empty layer array — also rejected (no layers → no rootfs).
  const std::string empty_layers = R"({
    "schemaVersion": 2,
    "mediaType": "application/vnd.oci.image.manifest.v1+json",
    "config": {
      "mediaType": "application/vnd.oci.image.config.v1+json",
      "digest": "sha256:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc",
      "size": 1471
    },
    "layers": []
  })";
  EXPECT_FALSE(parse_manifest(empty_layers).has_value());
}

TEST(ManifestTest, ImageManifest_ConfigDigest_RequiredFormat) {
  // Anything other than `sha256:<64-hex>` must be rejected for config.digest.
  const std::string bad_alg = R"({
    "schemaVersion": 2,
    "mediaType": "application/vnd.oci.image.manifest.v1+json",
    "config": {
      "mediaType": "application/vnd.oci.image.config.v1+json",
      "digest": "md5:d41d8cd98f00b204e9800998ecf8427e",
      "size": 0
    },
    "layers": [
      {
        "mediaType": "application/vnd.oci.image.layer.v1.tar+gzip",
        "digest": "sha256:1010101010101010101010101010101010101010101010101010101010101010",
        "size": 1024
      }
    ]
  })";
  EXPECT_FALSE(parse_manifest(bad_alg).has_value());

  // Too-short hex.
  const std::string short_hex = R"({
    "schemaVersion": 2,
    "mediaType": "application/vnd.oci.image.manifest.v1+json",
    "config": {
      "mediaType": "application/vnd.oci.image.config.v1+json",
      "digest": "sha256:abc123",
      "size": 0
    },
    "layers": [
      {
        "mediaType": "application/vnd.oci.image.layer.v1.tar+gzip",
        "digest": "sha256:1010101010101010101010101010101010101010101010101010101010101010",
        "size": 1024
      }
    ]
  })";
  EXPECT_FALSE(parse_manifest(short_hex).has_value());

  // Missing digest field.
  const std::string no_digest = R"({
    "schemaVersion": 2,
    "mediaType": "application/vnd.oci.image.manifest.v1+json",
    "config": {
      "mediaType": "application/vnd.oci.image.config.v1+json",
      "size": 0
    },
    "layers": [
      {
        "mediaType": "application/vnd.oci.image.layer.v1.tar+gzip",
        "digest": "sha256:1010101010101010101010101010101010101010101010101010101010101010",
        "size": 1024
      }
    ]
  })";
  EXPECT_FALSE(parse_manifest(no_digest).has_value());

  // Same rule applies to layers — a bad layer digest also fails.
  const std::string bad_layer = R"({
    "schemaVersion": 2,
    "mediaType": "application/vnd.oci.image.manifest.v1+json",
    "config": {
      "mediaType": "application/vnd.oci.image.config.v1+json",
      "digest": "sha256:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc",
      "size": 1471
    },
    "layers": [
      {
        "mediaType": "application/vnd.oci.image.layer.v1.tar+gzip",
        "digest": "notadigest",
        "size": 1024
      }
    ]
  })";
  EXPECT_FALSE(parse_manifest(bad_layer).has_value());
}

} // namespace
