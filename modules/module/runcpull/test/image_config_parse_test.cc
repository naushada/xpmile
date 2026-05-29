// Phase C, Step C.3 — image config blob parser.
// 5 tests with the `ImageConfig*` prefix.

#include "image_config.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

using runcpull::ImageConfig;
using runcpull::parse_image_config;

// Resolve a fixture file relative to this test source file. The test
// binary may run from anywhere (e.g. /src/build/test inside CI) so we
// compute the path off __FILE__ rather than a relative cwd.
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

// ── ImageConfigTest ───────────────────────────────────────────────────────

TEST(ImageConfigTest, ExtractsEnvAsListOfStrings) {
  auto cfg = parse_image_config(read_fixture("image_config.json"));
  ASSERT_TRUE(cfg.has_value());

  ASSERT_EQ(cfg->env.size(), 2u);
  EXPECT_EQ(cfg->env[0],
            "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin");
  EXPECT_EQ(cfg->env[1], "LANG=C.UTF-8");
}

TEST(ImageConfigTest, ExtractsCmdAndEntrypoint) {
  auto cfg = parse_image_config(read_fixture("image_config.json"));
  ASSERT_TRUE(cfg.has_value());

  ASSERT_EQ(cfg->entrypoint.size(), 1u);
  EXPECT_EQ(cfg->entrypoint[0], "/usr/local/bin/docker-entrypoint.sh");

  ASSERT_EQ(cfg->cmd.size(), 2u);
  EXPECT_EQ(cfg->cmd[0], "mongod");
  EXPECT_EQ(cfg->cmd[1], "--bind_ip_all");
}

TEST(ImageConfigTest, MissingEnvCmdEntrypoint_DefaultsEmpty) {
  // A perfectly valid OCI image config can have no runtime config at
  // all (e.g. a base layer with nothing to run). Parsing must succeed
  // and the four list fields must default to empty.
  const std::string minimal = R"({
    "architecture": "amd64",
    "os": "linux"
  })";

  auto cfg = parse_image_config(minimal);
  ASSERT_TRUE(cfg.has_value());

  EXPECT_TRUE(cfg->env.empty());
  EXPECT_TRUE(cfg->cmd.empty());
  EXPECT_TRUE(cfg->entrypoint.empty());
  EXPECT_TRUE(cfg->diff_ids.empty());

  // A document with a `config` block present but no Env/Cmd/Entrypoint
  // also produces empty vectors — distinct from "config absent".
  const std::string empty_config = R"({
    "config": {"WorkingDir": "/"},
    "rootfs": {"type": "layers", "diff_ids": []}
  })";
  auto cfg2 = parse_image_config(empty_config);
  ASSERT_TRUE(cfg2.has_value());
  EXPECT_TRUE(cfg2->env.empty());
  EXPECT_TRUE(cfg2->cmd.empty());
  EXPECT_TRUE(cfg2->entrypoint.empty());
  EXPECT_TRUE(cfg2->diff_ids.empty());
}

TEST(ImageConfigTest, ParsesRootfsDiffIds) {
  auto cfg = parse_image_config(read_fixture("image_config.json"));
  ASSERT_TRUE(cfg.has_value());

  ASSERT_EQ(cfg->diff_ids.size(), 3u);
  EXPECT_EQ(
      cfg->diff_ids[0],
      "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
  EXPECT_EQ(
      cfg->diff_ids[1],
      "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
  EXPECT_EQ(
      cfg->diff_ids[2],
      "sha256:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc");
}

TEST(ImageConfigTest, MalformedJson_Errors) {
  // Truncated JSON.
  EXPECT_FALSE(parse_image_config("{\"config\":").has_value());
  // Not an object at the top level.
  EXPECT_FALSE(parse_image_config("[1, 2, 3]").has_value());
  EXPECT_FALSE(parse_image_config("\"just a string\"").has_value());
  EXPECT_FALSE(parse_image_config("null").has_value());
  // Empty body.
  EXPECT_FALSE(parse_image_config("").has_value());
}

} // namespace
