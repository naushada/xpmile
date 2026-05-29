// Phase B.1 — Image reference parsing (8 tests).

#include <gtest/gtest.h>

#include "registry_client.hpp"

using runcpull::ImageRef;
using runcpull::parse_ref;
using runcpull::RefError;

TEST(RegistryRefTest, ShortName_DefaultsToLibraryNamespace) {
  auto r = parse_ref("mongo:4.4");
  ASSERT_EQ(r.error, RefError::NONE);
  EXPECT_EQ(r.ref.host, "docker.io");
  EXPECT_EQ(r.ref.name, "library/mongo");
  EXPECT_EQ(r.ref.tag,  "4.4");
  EXPECT_TRUE(r.ref.digest.empty());
}

TEST(RegistryRefTest, UserName_DefaultsToHubHost) {
  auto r = parse_ref("naushada/xpmile-wsdbagent");
  ASSERT_EQ(r.error, RefError::NONE);
  EXPECT_EQ(r.ref.host, "docker.io");
  EXPECT_EQ(r.ref.name, "naushada/xpmile-wsdbagent");
  EXPECT_EQ(r.ref.tag,  "latest");
}

TEST(RegistryRefTest, FullRef_PreservedExactly) {
  auto r = parse_ref("docker.io/naushada/xpmile-wsdbagent:v1.2.0");
  ASSERT_EQ(r.error, RefError::NONE);
  EXPECT_EQ(r.ref.host, "docker.io");
  EXPECT_EQ(r.ref.name, "naushada/xpmile-wsdbagent");
  EXPECT_EQ(r.ref.tag,  "v1.2.0");
}

TEST(RegistryRefTest, DigestRef_TagAbsent) {
  auto r = parse_ref(
      "naushada/foo@sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
  ASSERT_EQ(r.error, RefError::NONE);
  EXPECT_EQ(r.ref.name, "naushada/foo");
  EXPECT_TRUE(r.ref.tag.empty());
  EXPECT_FALSE(r.ref.digest.empty());
}

TEST(RegistryRefTest, TagAndDigest_DigestPreferred) {
  auto r = parse_ref(
      "naushada/foo:latest@sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
  ASSERT_EQ(r.error, RefError::NONE);
  EXPECT_TRUE(r.ref.tag.empty()) << "tag should be discarded when digest pins";
  EXPECT_FALSE(r.ref.digest.empty());
}

TEST(RegistryRefTest, InvalidName_Rejected) {
  // A name with unsupported characters (space inside).
  auto r = parse_ref("naushada/has a space");
  EXPECT_EQ(r.error, RefError::INVALID_CHARS);
}

TEST(RegistryRefTest, EmptyString_Rejected) {
  auto r = parse_ref("");
  EXPECT_EQ(r.error, RefError::EMPTY);
}

TEST(RegistryRefTest, NonHubRegistryHost_Rejected_InV1) {
  auto r = parse_ref("quay.io/foo/bar:latest");
  EXPECT_EQ(r.error, RefError::NON_HUB_REGISTRY);
}
