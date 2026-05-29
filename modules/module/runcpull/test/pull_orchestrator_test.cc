// Phase F — PullOrchestrator end-to-end (12 tests).

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "json.hpp"

#include "digest.hpp"
#include "in_memory_tar_builder.hpp"
#include "pull_orchestrator.hpp"
#include "registry_client.hpp"
#include "sso_http_client.hpp"

namespace fs = std::filesystem;

using runcpull::HostArch;
using runcpull::HostArchResult;
using runcpull::ImageRef;
using runcpull::PullError;
using runcpull::PullOptions;
using runcpull::PullOrchestrator;
using runcpull::PullResult;

namespace {

// ── MockHttpClient ────────────────────────────────────────────────────────
//
// Same shape as the Phase B test mock — canned responses keyed by URL,
// streaming overload delivers the body in one chunk, call log spies on
// URLs + headers. Anonymous namespace = no link collision with the other
// mocks elsewhere in the offtarget binary.

class MockHttpClient : public sso::IHttpClient {
 public:
  std::map<std::string, sso::HttpResponse> responses;

  struct Call {
    std::string url;
    std::map<std::string, std::string> headers;
  };
  std::vector<Call> calls;

  sso::HttpResponse get(const std::string &url) override {
    calls.push_back({url, {}});
    auto it = responses.find(url);
    return it != responses.end() ? it->second : sso::HttpResponse{};
  }
  sso::HttpResponse post_form(
      const std::string &,
      const std::map<std::string, std::string> &) override { return {}; }
  sso::HttpResponse get(
      const std::string &url,
      const std::map<std::string, std::string> &headers) override {
    calls.push_back({url, headers});
    auto it = responses.find(url);
    return it != responses.end() ? it->second : sso::HttpResponse{};
  }
  sso::HttpResponse get_streaming(
      const std::string &url,
      const std::map<std::string, std::string> &headers,
      const sso::BodyChunkCallback &on_chunk) override {
    calls.push_back({url, headers});
    sso::HttpResponse r;
    auto it = responses.find(url);
    if (it != responses.end()) {
      r.status  = it->second.status;
      r.headers = it->second.headers;
      if (on_chunk && !it->second.body.empty()) {
        on_chunk(it->second.body.data(), it->second.body.size());
      }
    }
    return r;
  }
};

// ── Hub URL builders ──────────────────────────────────────────────────────

constexpr const char *kReg = "https://registry-1.docker.io";

std::string manifest_url(const std::string &name, const std::string &ref) {
  return std::string(kReg) + "/v2/" + name + "/manifests/" + ref;
}
std::string blob_url(const std::string &name, const std::string &digest) {
  return std::string(kReg) + "/v2/" + name + "/blobs/" + digest;
}

// SHA-256 hex of a string, using the production Digest helper to keep
// the fixtures consistent with the orchestrator's verification path.
std::string sha256_hex(const std::string &s) {
  runcpull::Digest d;
  d.update(s);
  return d.finalize();
}
std::string sha_digest(const std::string &s) {
  return "sha256:" + sha256_hex(s);
}

// Wire the standard probe → /token sequence on the mock.
std::string wire_token(MockHttpClient &m, const std::string &image_name,
                          const std::string &token_value = "TOK") {
  sso::HttpResponse probe;
  probe.status = 401;
  probe.headers["www-authenticate"] =
      R"(Bearer realm="https://auth.docker.io/token",service="registry.docker.io")";
  m.responses[std::string(kReg) + "/v2/"] = probe;

  const std::string token_url = runcpull::build_token_url(
      "https://auth.docker.io/token", "registry.docker.io", image_name, "pull");
  sso::HttpResponse tok;
  tok.status = 200;
  tok.body   = std::string(R"({"token":")") + token_value + R"("})";
  m.responses[token_url] = tok;
  return token_url;
}

// Build a small image config blob with a known process spec.
std::string make_image_config() {
  nlohmann::json j;
  j["config"]["Env"]        = nlohmann::json::array({"PATH=/usr/bin"});
  j["config"]["Entrypoint"] = nlohmann::json::array({"/bin/sh"});
  j["config"]["Cmd"]        = nlohmann::json::array({"-c", "echo hi"});
  j["rootfs"]["diff_ids"]   = nlohmann::json::array();
  return j.dump();
}

// Build a single-arch image manifest pointing at one config + one layer.
std::string make_image_manifest(const std::string &config_digest,
                                  std::int64_t config_size,
                                  const std::string &layer_digest,
                                  std::int64_t layer_size,
                                  bool gzipped = false) {
  nlohmann::json j;
  j["schemaVersion"] = 2;
  j["mediaType"]     = "application/vnd.oci.image.manifest.v1+json";
  j["config"]        = {
      {"mediaType", "application/vnd.oci.image.config.v1+json"},
      {"digest",    config_digest},
      {"size",      config_size},
  };
  j["layers"] = nlohmann::json::array({nlohmann::json{
      {"mediaType",
        gzipped ? "application/vnd.oci.image.layer.v1.tar+gzip"
                 : "application/vnd.oci.image.layer.v1.tar"},
      {"digest", layer_digest},
      {"size",   layer_size},
  }});
  return j.dump();
}

// Multi-arch image index pointing at two per-arch manifests.
std::string make_image_index(const std::string &amd64_digest,
                                const std::string &arm64_digest) {
  nlohmann::json j;
  j["schemaVersion"] = 2;
  j["mediaType"]     = "application/vnd.oci.image.index.v1+json";
  j["manifests"]     = nlohmann::json::array({
      nlohmann::json{
          {"mediaType", "application/vnd.oci.image.manifest.v1+json"},
          {"digest", amd64_digest},
          {"size", 0},
          {"platform", {{"os", "linux"}, {"architecture", "amd64"}}}},
      nlohmann::json{
          {"mediaType", "application/vnd.oci.image.manifest.v1+json"},
          {"digest", arm64_digest},
          {"size", 0},
          {"platform", {{"os", "linux"}, {"architecture", "arm64"}}}},
  });
  return j.dump();
}

PullOptions opts_arm64() {
  PullOptions o;
  o.target.arch = HostArch::ARM64;
  o.target.name = "arm64";
  o.unpack.record_uidgid_metadata = false;
  return o;
}

fs::path fresh_bundle(const std::string &name) {
  fs::path p = fs::path(testing::TempDir()) / name;
  std::error_code ec;
  fs::remove_all(p, ec);
  return p;
}

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════
// F.1 — Happy paths
// ═══════════════════════════════════════════════════════════════════════════

TEST(PullTest, HappyPath_SingleLayer_BundleHasExpectedFile) {
  MockHttpClient m;
  wire_token(m, "library/alpine");

  // Single-layer tar with /etc/alpine-release.
  runcpull_test::InMemoryTarBuilder tar;
  tar.add_dir("etc/").add_file("etc/alpine-release", "3.19.0\n", 0644);
  const std::string layer_body   = tar.build();
  const std::string layer_digest = sha_digest(layer_body);

  const std::string cfg_body   = make_image_config();
  const std::string cfg_digest = sha_digest(cfg_body);

  const std::string mfn = make_image_manifest(
      cfg_digest, static_cast<std::int64_t>(cfg_body.size()),
      layer_digest, static_cast<std::int64_t>(layer_body.size()));

  // Single-arch manifest at tag "latest".
  sso::HttpResponse mfn_r; mfn_r.status = 200; mfn_r.body = mfn;
  mfn_r.headers["content-type"] = "application/vnd.oci.image.manifest.v1+json";
  m.responses[manifest_url("library/alpine", "latest")] = mfn_r;

  sso::HttpResponse cfg_r; cfg_r.status = 200; cfg_r.body = cfg_body;
  m.responses[blob_url("library/alpine", cfg_digest)] = cfg_r;
  sso::HttpResponse layer_r; layer_r.status = 200; layer_r.body = layer_body;
  m.responses[blob_url("library/alpine", layer_digest)] = layer_r;

  PullOrchestrator orch(m, opts_arm64());
  const fs::path bundle = fresh_bundle("happy-single-layer");
  ImageRef ref{"docker.io", "library/alpine", "latest", ""};

  PullResult r = orch.run(ref, bundle.string());
  EXPECT_EQ(r.error, PullError::NONE) << "detail=" << r.detail;
  EXPECT_EQ(r.layers_applied, 1u);
  EXPECT_TRUE(fs::exists(bundle / "rootfs" / "etc" / "alpine-release"));
}

TEST(PullTest, HappyPath_MultiLayer_LayersAppliedInOrder) {
  MockHttpClient m;
  wire_token(m, "naushada/foo");

  runcpull_test::InMemoryTarBuilder l1;
  l1.add_dir("etc/").add_file("etc/v", "L1\n", 0644);
  runcpull_test::InMemoryTarBuilder l2;
  l2.add_file("etc/v", "L2\n", 0644);
  runcpull_test::InMemoryTarBuilder l3;
  l3.add_file("etc/v", "L3\n", 0644);

  const std::string b1 = l1.build();
  const std::string b2 = l2.build();
  const std::string b3 = l3.build();
  const std::string d1 = sha_digest(b1);
  const std::string d2 = sha_digest(b2);
  const std::string d3 = sha_digest(b3);

  const std::string cfg_body   = make_image_config();
  const std::string cfg_digest = sha_digest(cfg_body);

  // 3-layer manifest.
  nlohmann::json j;
  j["schemaVersion"] = 2;
  j["mediaType"]     = "application/vnd.oci.image.manifest.v1+json";
  j["config"]        = {{"mediaType",
                          "application/vnd.oci.image.config.v1+json"},
                         {"digest", cfg_digest},
                         {"size",   static_cast<std::int64_t>(cfg_body.size())}};
  j["layers"] = nlohmann::json::array({
      {{"mediaType", "application/vnd.oci.image.layer.v1.tar"}, {"digest", d1}, {"size", b1.size()}},
      {{"mediaType", "application/vnd.oci.image.layer.v1.tar"}, {"digest", d2}, {"size", b2.size()}},
      {{"mediaType", "application/vnd.oci.image.layer.v1.tar"}, {"digest", d3}, {"size", b3.size()}},
  });
  const std::string mfn = j.dump();

  sso::HttpResponse mfn_r; mfn_r.status = 200; mfn_r.body = mfn;
  m.responses[manifest_url("naushada/foo", "latest")] = mfn_r;
  sso::HttpResponse cfg_r; cfg_r.status = 200; cfg_r.body = cfg_body;
  m.responses[blob_url("naushada/foo", cfg_digest)] = cfg_r;
  sso::HttpResponse r1; r1.status = 200; r1.body = b1;
  m.responses[blob_url("naushada/foo", d1)] = r1;
  sso::HttpResponse r2; r2.status = 200; r2.body = b2;
  m.responses[blob_url("naushada/foo", d2)] = r2;
  sso::HttpResponse r3; r3.status = 200; r3.body = b3;
  m.responses[blob_url("naushada/foo", d3)] = r3;

  PullOrchestrator orch(m, opts_arm64());
  const fs::path bundle = fresh_bundle("happy-multilayer");
  PullResult r = orch.run({"docker.io", "naushada/foo", "latest", ""},
                          bundle.string());
  ASSERT_EQ(r.error, PullError::NONE) << "detail=" << r.detail;
  EXPECT_EQ(r.layers_applied, 3u);

  // Final state of etc/v should be the L3 version.
  std::ifstream f(bundle / "rootfs" / "etc" / "v");
  std::string s((std::istreambuf_iterator<char>(f)),
                std::istreambuf_iterator<char>());
  EXPECT_EQ(s, "L3\n");
}

TEST(PullTest, HappyPath_MultiArchIndex_PicksHostArch) {
  MockHttpClient m;
  wire_token(m, "library/multi");

  // Per-arch manifests (we'll route by digest).
  runcpull_test::InMemoryTarBuilder amd_tar;
  amd_tar.add_file("which", "amd64\n");
  runcpull_test::InMemoryTarBuilder arm_tar;
  arm_tar.add_file("which", "arm64\n");
  const std::string amd_body = amd_tar.build();
  const std::string arm_body = arm_tar.build();
  const std::string amd_layer_d = sha_digest(amd_body);
  const std::string arm_layer_d = sha_digest(arm_body);

  const std::string cfg = make_image_config();
  const std::string cfg_d = sha_digest(cfg);

  const std::string amd_mfn = make_image_manifest(
      cfg_d, cfg.size(), amd_layer_d, amd_body.size());
  const std::string arm_mfn = make_image_manifest(
      cfg_d, cfg.size(), arm_layer_d, arm_body.size());
  const std::string amd_mfn_d = sha_digest(amd_mfn);
  const std::string arm_mfn_d = sha_digest(arm_mfn);

  const std::string index = make_image_index(amd_mfn_d, arm_mfn_d);

  // index at tag "latest", per-arch manifests at their digests, blobs.
  sso::HttpResponse idx_r; idx_r.status = 200; idx_r.body = index;
  m.responses[manifest_url("library/multi", "latest")] = idx_r;
  sso::HttpResponse amd_r; amd_r.status = 200; amd_r.body = amd_mfn;
  m.responses[manifest_url("library/multi", amd_mfn_d)] = amd_r;
  sso::HttpResponse arm_r; arm_r.status = 200; arm_r.body = arm_mfn;
  m.responses[manifest_url("library/multi", arm_mfn_d)] = arm_r;
  sso::HttpResponse cfg_r; cfg_r.status = 200; cfg_r.body = cfg;
  m.responses[blob_url("library/multi", cfg_d)] = cfg_r;
  sso::HttpResponse a_r; a_r.status = 200; a_r.body = arm_body;
  m.responses[blob_url("library/multi", arm_layer_d)] = a_r;

  PullOrchestrator orch(m, opts_arm64());
  const fs::path bundle = fresh_bundle("happy-multiarch");
  PullResult r = orch.run({"docker.io", "library/multi", "latest", ""},
                          bundle.string());
  ASSERT_EQ(r.error, PullError::NONE) << "detail=" << r.detail;
  EXPECT_EQ(r.selected_platform, "linux/arm64");
  std::ifstream f(bundle / "rootfs" / "which");
  std::string s((std::istreambuf_iterator<char>(f)),
                std::istreambuf_iterator<char>());
  EXPECT_EQ(s, "arm64\n");
}

TEST(PullTest, HappyPath_DigestRefSkipsIndexStep) {
  MockHttpClient m;
  wire_token(m, "naushada/x");

  runcpull_test::InMemoryTarBuilder tar;
  tar.add_file("ok", "x");
  const std::string layer = tar.build();
  const std::string ld    = sha_digest(layer);
  const std::string cfg = make_image_config();
  const std::string cd  = sha_digest(cfg);
  const std::string mfn = make_image_manifest(cd, cfg.size(), ld, layer.size());

  // The orchestrator is asked to pull a digest-pinned ref — it should
  // fetch `/manifests/<digest>` and treat the result as the canonical
  // image manifest. NO `/manifests/latest` request.
  const std::string pin = "sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
  sso::HttpResponse mfn_r; mfn_r.status = 200; mfn_r.body = mfn;
  m.responses[manifest_url("naushada/x", pin)] = mfn_r;
  sso::HttpResponse cfg_r; cfg_r.status = 200; cfg_r.body = cfg;
  m.responses[blob_url("naushada/x", cd)] = cfg_r;
  sso::HttpResponse lb_r; lb_r.status = 200; lb_r.body = layer;
  m.responses[blob_url("naushada/x", ld)] = lb_r;

  PullOrchestrator orch(m, opts_arm64());
  const fs::path bundle = fresh_bundle("happy-digestpin");
  PullResult r = orch.run({"docker.io", "naushada/x", "", pin},
                          bundle.string());
  EXPECT_EQ(r.error, PullError::NONE) << "detail=" << r.detail;

  for (const auto &c : m.calls) {
    EXPECT_EQ(c.url.find("manifests/latest"), std::string::npos)
        << "digest-pinned ref must not fetch the tag manifest: " << c.url;
  }
}

TEST(PullTest, HappyPath_OneTokenAcrossAllBlobFetches) {
  MockHttpClient m;
  const std::string token_url = wire_token(m, "naushada/y");

  runcpull_test::InMemoryTarBuilder tar;
  tar.add_file("a", "x");
  const std::string layer = tar.build();
  const std::string ld    = sha_digest(layer);
  const std::string cfg = make_image_config();
  const std::string cd  = sha_digest(cfg);
  const std::string mfn = make_image_manifest(cd, cfg.size(), ld, layer.size());

  sso::HttpResponse mfn_r; mfn_r.status = 200; mfn_r.body = mfn;
  m.responses[manifest_url("naushada/y", "latest")] = mfn_r;
  sso::HttpResponse cfg_r; cfg_r.status = 200; cfg_r.body = cfg;
  m.responses[blob_url("naushada/y", cd)] = cfg_r;
  sso::HttpResponse lb_r; lb_r.status = 200; lb_r.body = layer;
  m.responses[blob_url("naushada/y", ld)] = lb_r;

  PullOrchestrator orch(m, opts_arm64());
  PullResult r = orch.run({"docker.io", "naushada/y", "latest", ""},
                          fresh_bundle("happy-token-cache").string());
  EXPECT_EQ(r.error, PullError::NONE) << "detail=" << r.detail;

  std::size_t token_calls = 0;
  for (const auto &c : m.calls) if (c.url == token_url) ++token_calls;
  EXPECT_EQ(token_calls, 1u)
      << "orchestrator must share one token across config + every layer";
}

// ═══════════════════════════════════════════════════════════════════════════
// F.2 — Failure modes (atomicity)
// ═══════════════════════════════════════════════════════════════════════════

TEST(PullTest, LayerHttp500_AbortsAndLeavesNoBundle) {
  MockHttpClient m;
  wire_token(m, "naushada/fail");

  runcpull_test::InMemoryTarBuilder tar;
  tar.add_file("ok", "x");
  const std::string layer = tar.build();
  const std::string ld    = sha_digest(layer);
  const std::string cfg = make_image_config();
  const std::string cd  = sha_digest(cfg);
  const std::string mfn = make_image_manifest(cd, cfg.size(), ld, layer.size());

  sso::HttpResponse mfn_r; mfn_r.status = 200; mfn_r.body = mfn;
  m.responses[manifest_url("naushada/fail", "latest")] = mfn_r;
  sso::HttpResponse cfg_r; cfg_r.status = 200; cfg_r.body = cfg;
  m.responses[blob_url("naushada/fail", cd)] = cfg_r;
  // Layer blob returns 500.
  sso::HttpResponse lb_r; lb_r.status = 500;
  m.responses[blob_url("naushada/fail", ld)] = lb_r;

  PullOrchestrator orch(m, opts_arm64());
  const fs::path bundle = fresh_bundle("fail-layer-500");
  PullResult r = orch.run({"docker.io", "naushada/fail", "latest", ""},
                          bundle.string());
  EXPECT_EQ(r.error, PullError::TRANSPORT);
  EXPECT_FALSE(fs::exists(bundle));
}

TEST(PullTest, ConfigDigestMismatch_AbortsAndLeavesNoBundle) {
  MockHttpClient m;
  wire_token(m, "naushada/cdm");

  const std::string real_cfg = make_image_config();
  const std::string fake_cfg = real_cfg + "\n";   // different bytes
  const std::string cd       = sha_digest(real_cfg);

  runcpull_test::InMemoryTarBuilder tar;
  tar.add_file("a", "x");
  const std::string layer = tar.build();
  const std::string ld    = sha_digest(layer);
  const std::string mfn   = make_image_manifest(cd, real_cfg.size(), ld, layer.size());

  sso::HttpResponse mfn_r; mfn_r.status = 200; mfn_r.body = mfn;
  m.responses[manifest_url("naushada/cdm", "latest")] = mfn_r;
  // Serve the WRONG config bytes.
  sso::HttpResponse cfg_r; cfg_r.status = 200; cfg_r.body = fake_cfg;
  m.responses[blob_url("naushada/cdm", cd)] = cfg_r;

  PullOrchestrator orch(m, opts_arm64());
  const fs::path bundle = fresh_bundle("fail-config-digest");
  PullResult r = orch.run({"docker.io", "naushada/cdm", "latest", ""},
                          bundle.string());
  EXPECT_EQ(r.error, PullError::CONFIG_DIGEST_MISMATCH);
  EXPECT_FALSE(fs::exists(bundle));
}

TEST(PullTest, LayerDigestMismatch_AbortsAndLeavesNoBundle) {
  MockHttpClient m;
  wire_token(m, "naushada/ldm");

  runcpull_test::InMemoryTarBuilder real_tar;
  real_tar.add_file("a", "real");
  const std::string real_layer = real_tar.build();
  const std::string ld         = sha_digest(real_layer);

  // Serve a corrupted body for the same digest.
  runcpull_test::InMemoryTarBuilder fake_tar;
  fake_tar.add_file("a", "TAMPERED");
  const std::string fake_layer = fake_tar.build();

  const std::string cfg = make_image_config();
  const std::string cd  = sha_digest(cfg);
  const std::string mfn = make_image_manifest(cd, cfg.size(), ld, real_layer.size());

  sso::HttpResponse mfn_r; mfn_r.status = 200; mfn_r.body = mfn;
  m.responses[manifest_url("naushada/ldm", "latest")] = mfn_r;
  sso::HttpResponse cfg_r; cfg_r.status = 200; cfg_r.body = cfg;
  m.responses[blob_url("naushada/ldm", cd)] = cfg_r;
  sso::HttpResponse lb_r; lb_r.status = 200; lb_r.body = fake_layer;
  m.responses[blob_url("naushada/ldm", ld)] = lb_r;

  PullOrchestrator orch(m, opts_arm64());
  const fs::path bundle = fresh_bundle("fail-layer-digest");
  PullResult r = orch.run({"docker.io", "naushada/ldm", "latest", ""},
                          bundle.string());
  EXPECT_EQ(r.error, PullError::LAYER_DIGEST_MISMATCH);
  EXPECT_FALSE(fs::exists(bundle));
}

TEST(PullTest, NoMatchingArch_AbortsBeforeAnyDownload) {
  MockHttpClient m;
  wire_token(m, "library/multi");

  // Index with linux/amd64 only — host is arm64 → no match.
  const std::string amd_mfn = make_image_manifest(
      "sha256:0000000000000000000000000000000000000000000000000000000000000000",
      0, "sha256:1111111111111111111111111111111111111111111111111111111111111111", 0);
  const std::string amd_mfn_d = sha_digest(amd_mfn);
  nlohmann::json idx_j;
  idx_j["schemaVersion"] = 2;
  idx_j["mediaType"]     = "application/vnd.oci.image.index.v1+json";
  idx_j["manifests"]     = nlohmann::json::array({nlohmann::json{
      {"mediaType", "application/vnd.oci.image.manifest.v1+json"},
      {"digest",    amd_mfn_d},
      {"size",      0},
      {"platform",  {{"os", "linux"}, {"architecture", "amd64"}}}}});
  sso::HttpResponse idx_r; idx_r.status = 200; idx_r.body = idx_j.dump();
  m.responses[manifest_url("library/multi", "latest")] = idx_r;

  PullOrchestrator orch(m, opts_arm64());
  const fs::path bundle = fresh_bundle("fail-no-arch");
  PullResult r = orch.run({"docker.io", "library/multi", "latest", ""},
                          bundle.string());
  EXPECT_EQ(r.error, PullError::NO_MATCHING_PLATFORM);
  EXPECT_FALSE(fs::exists(bundle));

  // Verify no blob fetches occurred — the call log should contain probe,
  // token, the index manifest, and nothing else.
  for (const auto &c : m.calls) {
    EXPECT_EQ(c.url.find("/blobs/"), std::string::npos)
        << "no /blobs/ call should happen after platform-select failure";
  }
}

TEST(PullTest, TokenEndpoint403_AbortsBeforeAnyManifestFetch) {
  MockHttpClient m;

  // Probe returns challenge, /token returns 403.
  sso::HttpResponse probe;
  probe.status = 401;
  probe.headers["www-authenticate"] =
      R"(Bearer realm="https://auth.docker.io/token",service="registry.docker.io")";
  m.responses[std::string(kReg) + "/v2/"] = probe;

  const std::string token_url = runcpull::build_token_url(
      "https://auth.docker.io/token", "registry.docker.io",
      "naushada/forbidden", "pull");
  sso::HttpResponse tok; tok.status = 403; tok.body = "{}";
  m.responses[token_url] = tok;

  PullOrchestrator orch(m, opts_arm64());
  const fs::path bundle = fresh_bundle("fail-token-403");
  PullResult r = orch.run({"docker.io", "naushada/forbidden", "latest", ""},
                          bundle.string());
  EXPECT_EQ(r.error, PullError::AUTH);
  EXPECT_FALSE(fs::exists(bundle));

  // Make sure we never hit /manifests/ — the call log should be probe + token
  // only.
  for (const auto &c : m.calls) {
    EXPECT_EQ(c.url.find("/manifests/"), std::string::npos)
        << "no manifest call should happen after token failure";
  }
}

TEST(PullTest, ManifestUnsupportedMediaType_AbortsWithManifestError) {
  MockHttpClient m;
  wire_token(m, "library/weird");

  // Return a body whose mediaType is not one of the four we support.
  nlohmann::json j;
  j["schemaVersion"] = 1;
  j["mediaType"]     = "application/vnd.unknown.weird+json";
  j["layers"]        = nlohmann::json::array();
  sso::HttpResponse mfn_r; mfn_r.status = 200; mfn_r.body = j.dump();
  m.responses[manifest_url("library/weird", "latest")] = mfn_r;

  PullOrchestrator orch(m, opts_arm64());
  const fs::path bundle = fresh_bundle("fail-bad-media");
  PullResult r = orch.run({"docker.io", "library/weird", "latest", ""},
                          bundle.string());
  EXPECT_EQ(r.error, PullError::MANIFEST_PARSE);
  EXPECT_FALSE(fs::exists(bundle));
}

TEST(PullTest, UnpackPathTraversal_AbortsAndLeavesNoBundle) {
  MockHttpClient m;
  wire_token(m, "naushada/evil");

  // Layer with a `..` entry.
  runcpull_test::InMemoryTarBuilder tar;
  tar.add_file("../escape", "PWNED");
  const std::string layer = tar.build();
  const std::string ld    = sha_digest(layer);

  const std::string cfg = make_image_config();
  const std::string cd  = sha_digest(cfg);
  const std::string mfn = make_image_manifest(cd, cfg.size(), ld, layer.size());

  sso::HttpResponse mfn_r; mfn_r.status = 200; mfn_r.body = mfn;
  m.responses[manifest_url("naushada/evil", "latest")] = mfn_r;
  sso::HttpResponse cfg_r; cfg_r.status = 200; cfg_r.body = cfg;
  m.responses[blob_url("naushada/evil", cd)] = cfg_r;
  sso::HttpResponse lb_r; lb_r.status = 200; lb_r.body = layer;
  m.responses[blob_url("naushada/evil", ld)] = lb_r;

  PullOrchestrator orch(m, opts_arm64());
  const fs::path bundle = fresh_bundle("fail-traversal");
  PullResult r = orch.run({"docker.io", "naushada/evil", "latest", ""},
                          bundle.string());
  EXPECT_EQ(r.error, PullError::UNPACK_FAILED);
  EXPECT_FALSE(fs::exists(bundle));
}
