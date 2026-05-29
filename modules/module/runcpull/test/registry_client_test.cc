// Phase B.3 — RegistryClient against a MockHttpClient (7 tests).

#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

#include "registry_client.hpp"
#include "sso_http_client.hpp"

using runcpull::ImageRef;
using runcpull::ManifestFetch;
using runcpull::RegistryClient;
using runcpull::RegistryError;

namespace {

// Local mock that records last-call state and routes canned responses by
// URL. Anonymous namespace → no link collision with the other mocks in
// sso_test.cc / sso_http_client_ext_test.cc.
class MockHttpClient : public sso::IHttpClient {
 public:
  // URL → canned response. Caller can pre-fill status, body, and headers
  // (e.g. Www-Authenticate, Location).
  std::map<std::string, sso::HttpResponse> responses;

  // Spy: ordered log of every (url, headers) the registry made.
  struct Call {
    std::string url;
    std::map<std::string, std::string> headers;
  };
  std::vector<Call> calls;

  // Plain GET (no headers) — used for the probe + token endpoints.
  sso::HttpResponse get(const std::string &url) override {
    calls.push_back({url, {}});
    const auto it = responses.find(url);
    return it != responses.end() ? it->second : sso::HttpResponse{};
  }

  sso::HttpResponse post_form(
      const std::string &,
      const std::map<std::string, std::string> &) override { return {}; }

  // Headers overload — used for manifest fetches.
  sso::HttpResponse get(
      const std::string &url,
      const std::map<std::string, std::string> &headers) override {
    calls.push_back({url, headers});
    const auto it = responses.find(url);
    return it != responses.end() ? it->second : sso::HttpResponse{};
  }

  // Streaming overload — used for blobs. Delivers `streamingChunks` if set,
  // otherwise the canned body in one chunk.
  std::vector<std::string> streamingChunks;
  sso::HttpResponse get_streaming(
      const std::string &url,
      const std::map<std::string, std::string> &headers,
      const sso::BodyChunkCallback &on_chunk) override {
    calls.push_back({url, headers});
    sso::HttpResponse r;
    const auto it = responses.find(url);
    if (it != responses.end()) {
      r.status  = it->second.status;
      r.headers = it->second.headers;
    }
    if (on_chunk) {
      if (!streamingChunks.empty()) {
        for (const auto &c : streamingChunks) on_chunk(c.data(), c.size());
      } else if (it != responses.end() && !it->second.body.empty()) {
        on_chunk(it->second.body.data(), it->second.body.size());
      }
    }
    return r;
  }
};

// Pre-fill a mock with the standard Hub probe → token sequence so each
// test doesn't have to. Returns the mock by value? We mutate by reference.
void wire_standard_token_flow(MockHttpClient &m) {
  sso::HttpResponse probe;
  probe.status = 401;
  probe.headers["www-authenticate"] =
      R"(Bearer realm="https://auth.docker.io/token",service="registry.docker.io")";
  m.responses["https://registry-1.docker.io/v2/"] = probe;

  // Token URL — fixed by build_token_url(realm, service, "library/mongo", "pull").
  // We just match prefix below by walking the recorded calls instead of
  // computing the exact URL here.
  // Provide a wildcard via specific URL set by the test if needed.
}

// Find the token URL in the issued calls (first auth.docker.io GET) and
// wire its response.
void wire_token_for(MockHttpClient &m, const std::string &token_url,
                     const std::string &bearer) {
  sso::HttpResponse tok;
  tok.status = 200;
  tok.body   = std::string(R"({"token":")") + bearer + R"("})";
  m.responses[token_url] = tok;
}

}  // namespace

// ── B.3.1 Probe_GetsTokenFromChallenge ────────────────────────────────────

TEST(RegistryClientTest, Probe_GetsTokenFromChallenge) {
  MockHttpClient m;
  wire_standard_token_flow(m);
  // Compute the token URL the same way build_token_url does.
  const std::string token_url = runcpull::build_token_url(
      "https://auth.docker.io/token",
      "registry.docker.io",
      "library/mongo",
      "pull");
  wire_token_for(m, token_url, "TOK");

  // Manifest URL: 200 stub.
  sso::HttpResponse mfn;
  mfn.status = 200;
  mfn.body   = R"({"mediaType":"application/vnd.oci.image.manifest.v1+json"})";
  mfn.headers["content-type"] = "application/vnd.oci.image.manifest.v1+json";
  m.responses["https://registry-1.docker.io/v2/library/mongo/manifests/4.4"] = mfn;

  ImageRef ref{"docker.io", "library/mongo", "4.4", ""};
  RegistryClient c(m, ref);
  const ManifestFetch r = c.fetch_manifest("4.4");
  EXPECT_EQ(r.error, RegistryError::NONE);
  EXPECT_EQ(c.cached_token(), "TOK");
}

// ── B.3.2 Manifest_SetsAllAcceptVariants ──────────────────────────────────

TEST(RegistryClientTest, Manifest_SetsAllAcceptVariants) {
  MockHttpClient m;
  wire_standard_token_flow(m);
  const std::string token_url = runcpull::build_token_url(
      "https://auth.docker.io/token", "registry.docker.io",
      "library/mongo", "pull");
  wire_token_for(m, token_url, "TOK");

  sso::HttpResponse mfn; mfn.status = 200; mfn.body = "{}";
  m.responses["https://registry-1.docker.io/v2/library/mongo/manifests/4.4"] = mfn;

  RegistryClient c(m, ImageRef{"docker.io", "library/mongo", "4.4", ""});
  c.fetch_manifest("4.4");

  // Find the manifest call and inspect its Accept header.
  const std::map<std::string, std::string> *manifest_headers = nullptr;
  for (const auto &call : m.calls) {
    if (call.url.find("/manifests/") != std::string::npos) {
      manifest_headers = &call.headers;
      break;
    }
  }
  ASSERT_NE(manifest_headers, nullptr);
  const auto it = manifest_headers->find("Accept");
  ASSERT_NE(it, manifest_headers->end());
  const std::string &accept = it->second;
  EXPECT_NE(accept.find("oci.image.index.v1"),               std::string::npos);
  EXPECT_NE(accept.find("manifest.list.v2"),                 std::string::npos);
  EXPECT_NE(accept.find("oci.image.manifest.v1"),            std::string::npos);
  EXPECT_NE(accept.find("manifest.v2"),                      std::string::npos);
}

// ── B.3.3 Manifest_SetsBearerAuthorization ────────────────────────────────

TEST(RegistryClientTest, Manifest_SetsBearerAuthorization) {
  MockHttpClient m;
  wire_standard_token_flow(m);
  const std::string token_url = runcpull::build_token_url(
      "https://auth.docker.io/token", "registry.docker.io",
      "library/mongo", "pull");
  wire_token_for(m, token_url, "TOK");
  sso::HttpResponse mfn; mfn.status = 200; mfn.body = "{}";
  m.responses["https://registry-1.docker.io/v2/library/mongo/manifests/4.4"] = mfn;

  RegistryClient c(m, ImageRef{"docker.io", "library/mongo", "4.4", ""});
  c.fetch_manifest("4.4");

  for (const auto &call : m.calls) {
    if (call.url.find("/manifests/") != std::string::npos) {
      auto it = call.headers.find("Authorization");
      ASSERT_NE(it, call.headers.end());
      EXPECT_EQ(it->second, "Bearer TOK");
      return;
    }
  }
  FAIL() << "no /manifests/ call observed";
}

// ── B.3.4 Manifest_404_ReturnsManifestNotFound ────────────────────────────

TEST(RegistryClientTest, Manifest_404_ReturnsManifestNotFound) {
  MockHttpClient m;
  wire_standard_token_flow(m);
  const std::string token_url = runcpull::build_token_url(
      "https://auth.docker.io/token", "registry.docker.io",
      "library/mongo", "pull");
  wire_token_for(m, token_url, "TOK");
  sso::HttpResponse mfn; mfn.status = 404;
  m.responses["https://registry-1.docker.io/v2/library/mongo/manifests/nope"] = mfn;

  RegistryClient c(m, ImageRef{"docker.io", "library/mongo", "nope", ""});
  const ManifestFetch r = c.fetch_manifest("nope");
  EXPECT_EQ(r.error, RegistryError::MANIFEST_NOT_FOUND);
}

// ── B.3.5 Blob_FollowsCdnRedirect_DropsBearer ─────────────────────────────

TEST(RegistryClientTest, Blob_FollowsCdnRedirect_DropsBearer) {
  MockHttpClient m;
  wire_standard_token_flow(m);
  const std::string token_url = runcpull::build_token_url(
      "https://auth.docker.io/token", "registry.docker.io",
      "naushada/x", "pull");
  wire_token_for(m, token_url, "TOK");

  // Blob URL on Hub: 307 to CDN.
  const std::string blob_url =
      "https://registry-1.docker.io/v2/naushada/x/blobs/sha256:abc";
  sso::HttpResponse redirect;
  redirect.status = 307;
  redirect.headers["Location"] = "https://cdn.example/abc";
  m.responses[blob_url] = redirect;

  // CDN URL: 200 with the actual body.
  sso::HttpResponse cdn;
  cdn.status = 200;
  cdn.body   = "blob-bytes";
  m.responses["https://cdn.example/abc"] = cdn;

  RegistryClient c(m, ImageRef{"docker.io", "naushada/x", "latest", ""});
  std::string acc;
  c.fetch_blob("sha256:abc",
               [&](const char *d, std::size_t n) { acc.append(d, n); });
  EXPECT_EQ(acc, "blob-bytes");

  // Find the CDN call and assert Authorization is NOT in its headers.
  for (const auto &call : m.calls) {
    if (call.url == "https://cdn.example/abc") {
      EXPECT_EQ(call.headers.count("Authorization"), 0u)
          << "Bearer must be dropped on cross-origin CDN hop";
      return;
    }
  }
  FAIL() << "no CDN call observed";
}

// ── B.3.6 Blob_StreamsBodyToCallback ──────────────────────────────────────

TEST(RegistryClientTest, Blob_StreamsBodyToCallback) {
  MockHttpClient m;
  wire_standard_token_flow(m);
  const std::string token_url = runcpull::build_token_url(
      "https://auth.docker.io/token", "registry.docker.io",
      "naushada/x", "pull");
  wire_token_for(m, token_url, "TOK");

  const std::string url =
      "https://registry-1.docker.io/v2/naushada/x/blobs/sha256:def";
  sso::HttpResponse ok; ok.status = 200;
  m.responses[url] = ok;
  m.streamingChunks = {"hello, ", "runc-", "pull"};

  RegistryClient c(m, ImageRef{"docker.io", "naushada/x", "latest", ""});
  std::string acc;
  const RegistryError e = c.fetch_blob("sha256:def",
      [&](const char *d, std::size_t n) { acc.append(d, n); });
  EXPECT_EQ(e, RegistryError::NONE);
  EXPECT_EQ(acc, "hello, runc-pull");
}

// ── B.3.7 Probe_Token_Caching_OneTokenPerImage ────────────────────────────

TEST(RegistryClientTest, Probe_Token_Caching_OneTokenPerImage) {
  MockHttpClient m;
  wire_standard_token_flow(m);
  const std::string token_url = runcpull::build_token_url(
      "https://auth.docker.io/token", "registry.docker.io",
      "library/mongo", "pull");
  wire_token_for(m, token_url, "TOK");
  sso::HttpResponse mfn; mfn.status = 200; mfn.body = "{}";
  m.responses["https://registry-1.docker.io/v2/library/mongo/manifests/4.4"] = mfn;
  m.responses["https://registry-1.docker.io/v2/library/mongo/manifests/sha256:abc"] = mfn;

  RegistryClient c(m, ImageRef{"docker.io", "library/mongo", "4.4", ""});
  c.fetch_manifest("4.4");
  c.fetch_manifest("sha256:abc");
  c.fetch_manifest("4.4");

  // Count how many times the token URL was hit.
  std::size_t token_calls = 0;
  for (const auto &call : m.calls) {
    if (call.url == token_url) ++token_calls;
  }
  EXPECT_EQ(token_calls, 1u)
      << "RegistryClient must reuse the bearer for the same image";
}
