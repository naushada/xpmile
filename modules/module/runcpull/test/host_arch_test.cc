// Phase B.4 — Host architecture probing (5 tests).

#include <gtest/gtest.h>

#include <string>

#include "registry_client.hpp"

using runcpull::HostArch;
using runcpull::IHostProbe;
using runcpull::resolve_host_arch;
using runcpull::resolve_host_arch_from_override;

namespace {

class FakeProbe : public IHostProbe {
 public:
  explicit FakeProbe(std::string m) : m_machine(std::move(m)) {}
  std::string uname_m() const override { return m_machine; }
 private:
  std::string m_machine;
};

}  // namespace

TEST(HostArchTest, Probe_x86_64_MapsToAmd64) {
  FakeProbe p("x86_64");
  auto r = resolve_host_arch(p);
  EXPECT_EQ(r.arch, HostArch::AMD64);
  EXPECT_EQ(r.name, "amd64");
  EXPECT_TRUE(r.variant.empty());
}

TEST(HostArchTest, Probe_aarch64_MapsToArm64) {
  FakeProbe p("aarch64");
  auto r = resolve_host_arch(p);
  EXPECT_EQ(r.arch, HostArch::ARM64);
  EXPECT_EQ(r.name, "arm64");
  EXPECT_TRUE(r.variant.empty());
}

TEST(HostArchTest, Probe_armv7l_MapsToArmV7) {
  FakeProbe p("armv7l");
  auto r = resolve_host_arch(p);
  EXPECT_EQ(r.arch,    HostArch::ARMV7);
  EXPECT_EQ(r.name,    "arm");
  EXPECT_EQ(r.variant, "v7");
}

TEST(HostArchTest, Probe_Unknown_Errors) {
  FakeProbe p("mips64");
  auto r = resolve_host_arch(p);
  EXPECT_EQ(r.arch, HostArch::UNKNOWN);
}

TEST(HostArchTest, CliOverride_BeatsAutoDetect) {
  // Override returns the requested arch regardless of what the probe
  // would have said.
  auto a = resolve_host_arch_from_override("arm64");
  EXPECT_EQ(a.arch, HostArch::ARM64);
  EXPECT_EQ(a.name, "arm64");

  auto b = resolve_host_arch_from_override("arm/v7");
  EXPECT_EQ(b.arch,    HostArch::ARMV7);
  EXPECT_EQ(b.variant, "v7");
}
