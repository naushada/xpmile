// Phase A — sso::HttpClient extensions for runc-pull.
// See docs/design/runc-pull/runc-pull-tdd-plan.md §"Phase A".
//
//   A.1 (3 tests) — MockHttpClient captures custom request headers
//   A.2 (7 tests) — redirect_step() pure-function policy
//   A.3 (3 tests) — streaming-callback get_streaming() shape
//
// The real sso::HttpClient (ACE_SOCK + OpenSSL) is integration-verified, not
// unit-tested — same precedent as Phase B in docs/design/sso/sso-tdd-plan.md.

#include "sso_http_client.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

// Test-local MockHttpClient. The mock in sso_test.cc only implements the
// original IHttpClient surface; this one extends it with header capture and
// streaming-chunk control for the Phase A tests. Lives in an anonymous
// namespace so it doesn't collide at link time with the older mock.
class MockHttpClient : public sso::IHttpClient {
public:
  // Canned response routing — keyed by URL like the sso_test.cc mock.
  std::map<std::string, sso::HttpResponse> responses;

  // Spy fields the tests assert against.
  std::string                        lastGetUrl;
  std::map<std::string, std::string> lastGetHeaders;

  // Optional canned chunks for the streaming overload — when non-empty, each
  // entry is delivered via on_chunk() in order, instead of falling back to
  // the buffered get() body.
  std::vector<std::string> streamingChunks;

  // Original IHttpClient surface (still mandatory).
  sso::HttpResponse get(const std::string &url) override {
    lastGetUrl = url;
    lastGetHeaders.clear();
    const auto it = responses.find(url);
    return it != responses.end() ? it->second : sso::HttpResponse{};
  }
  sso::HttpResponse post_form(
      const std::string &,
      const std::map<std::string, std::string> &) override {
    return sso::HttpResponse{};
  }

  // Phase A.1 — capture custom request headers.
  sso::HttpResponse get(
      const std::string &url,
      const std::map<std::string, std::string> &headers) override {
    lastGetUrl     = url;
    lastGetHeaders = headers;
    const auto it = responses.find(url);
    return it != responses.end() ? it->second : sso::HttpResponse{};
  }

  // Phase A.3 — deliver canned body in N chunks, propagate callback throws.
  sso::HttpResponse get_streaming(
      const std::string &url,
      const std::map<std::string, std::string> &headers,
      const sso::BodyChunkCallback &on_chunk) override {
    lastGetUrl     = url;
    lastGetHeaders = headers;
    sso::HttpResponse r;
    const auto it = responses.find(url);
    if (it != responses.end()) {
      r.status  = it->second.status;
      r.headers = it->second.headers;
    }
    if (on_chunk) {
      // If canned chunks present, deliver them sequentially. Otherwise fall
      // back to whatever the canned response body holds, in one chunk.
      if (!streamingChunks.empty()) {
        for (const auto &c : streamingChunks) {
          on_chunk(c.data(), c.size());  // exception propagates to caller
        }
      } else if (it != responses.end() && !it->second.body.empty()) {
        on_chunk(it->second.body.data(), it->second.body.size());
      }
    }
    return r;
  }
};

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════
// A.1 — MockHttpClient captures custom request headers
// ═══════════════════════════════════════════════════════════════════════════

TEST(HttpClientHeadersTest, MockClient_RecordsRequestHeaders) {
  MockHttpClient http;
  http.get("https://registry-1.docker.io/v2/foo/manifests/latest",
           {{"Authorization", "Bearer tok"},
            {"Accept",        "application/vnd.oci.image.index.v1+json"}});
  EXPECT_EQ(http.lastGetUrl, "https://registry-1.docker.io/v2/foo/manifests/latest");
  EXPECT_EQ(http.lastGetHeaders.at("Authorization"), "Bearer tok");
  EXPECT_EQ(http.lastGetHeaders.at("Accept"),
            "application/vnd.oci.image.index.v1+json");
}

TEST(HttpClientHeadersTest, MockClient_HeadersDefaultEmpty_WhenNoneSet) {
  MockHttpClient http;
  http.get("https://x/y");
  EXPECT_TRUE(http.lastGetHeaders.empty());
}

TEST(HttpClientHeadersTest, MockClient_LastHeadersReplaced_OnNextCall) {
  MockHttpClient http;
  http.get("https://a/", {{"X-One", "1"}});
  EXPECT_EQ(http.lastGetHeaders.count("X-One"), 1u);

  http.get("https://b/", {{"X-Two", "2"}});
  EXPECT_EQ(http.lastGetHeaders.count("X-One"), 0u);
  EXPECT_EQ(http.lastGetHeaders.at("X-Two"), "2");
}

// ═══════════════════════════════════════════════════════════════════════════
// A.2 — redirect_step() per-step policy
// ═══════════════════════════════════════════════════════════════════════════

TEST(HttpClientRedirectTest, AbsoluteLocation_UsedAsIs) {
  const auto s = sso::redirect_step("https://example.com/a", 302,
                                     "https://other.com/b", {});
  EXPECT_TRUE(s.follow);
  EXPECT_EQ(s.next_url, "https://other.com/b");
}

TEST(HttpClientRedirectTest, RelativeLocation_ResolvedAgainstCurrent) {
  // Root-relative ("/b/y") → scheme + host + Location.
  const auto root = sso::redirect_step("https://example.com/a/x", 302,
                                        "/b/y", {});
  EXPECT_TRUE(root.follow);
  EXPECT_EQ(root.next_url, "https://example.com/b/y");

  // Path-relative ("b") → resolve against current's directory.
  const auto rel = sso::redirect_step("https://example.com/a/x", 302,
                                       "b", {});
  EXPECT_TRUE(rel.follow);
  EXPECT_EQ(rel.next_url, "https://example.com/a/b");
}

TEST(HttpClientRedirectTest, SameOriginRedirect_KeepsAuthorization) {
  const auto s = sso::redirect_step("https://example.com/a", 302,
                                     "https://example.com/b",
                                     {{"Authorization", "Bearer tok"},
                                      {"Accept",        "application/json"}});
  EXPECT_TRUE(s.follow);
  EXPECT_EQ(s.headers.at("Authorization"), "Bearer tok");
  EXPECT_EQ(s.headers.at("Accept"),        "application/json");
}

TEST(HttpClientRedirectTest, CrossOriginRedirect_DropsAuthorization) {
  const auto s = sso::redirect_step("https://registry-1.docker.io/v2/x/blob/abc",
                                     307,
                                     "https://cdn.cloudfront.net/abc",
                                     {{"Authorization", "Bearer tok"},
                                      {"Accept",        "*/*"}});
  EXPECT_TRUE(s.follow);
  EXPECT_EQ(s.headers.count("Authorization"), 0u);
  // Non-auth headers survive the hop.
  EXPECT_EQ(s.headers.at("Accept"), "*/*");
}

TEST(HttpClientRedirectTest, HopLimitExceeded_Errors) {
  // redirect_step is per-hop pure; this test exercises a caller loop's hop
  // discipline — after the loop refuses to follow on the 5th iteration, the
  // chain should be considered exceeded.
  constexpr int max_hops = 5;
  std::string url        = "https://chain.example/0";
  int  hops              = 0;
  bool aborted_at_limit  = false;
  while (true) {
    if (hops >= max_hops) { aborted_at_limit = true; break; }
    const auto step = sso::redirect_step(url, 302,
                                          "/" + std::to_string(hops + 1), {});
    if (!step.follow) break;
    url = step.next_url;
    ++hops;
  }
  EXPECT_TRUE(aborted_at_limit);
  EXPECT_EQ(hops, max_hops);
}

TEST(HttpClientRedirectTest, StatusCodes_301_302_307_308_AllFollowed) {
  for (long s : {301L, 302L, 307L, 308L}) {
    const auto r = sso::redirect_step("https://x/a", s, "https://x/b", {});
    EXPECT_TRUE(r.follow) << "status " << s << " should follow";
    EXPECT_FALSE(r.downgrade_to_get) << "status " << s << " keeps method";
  }
}

TEST(HttpClientRedirectTest, Status303_RedirectAsGet_BodyDropped) {
  const auto r = sso::redirect_step("https://x/a", 303, "https://x/b", {});
  EXPECT_TRUE(r.follow);
  EXPECT_TRUE(r.downgrade_to_get)
      << "303 signals the caller to drop the request body";
}

// ═══════════════════════════════════════════════════════════════════════════
// A.3 — streaming body callback shape
// ═══════════════════════════════════════════════════════════════════════════

TEST(HttpClientStreamingTest, Callback_ReceivesFullBody_AcrossChunks) {
  MockHttpClient http;
  sso::HttpResponse canned;
  canned.status = 200;
  http.responses["https://x/blob"] = canned;
  http.streamingChunks = {"foo", "bar", "baz"};

  std::string acc;
  const auto r = http.get_streaming(
      "https://x/blob", {},
      [&](const char *d, std::size_t n) { acc.append(d, n); });
  EXPECT_EQ(r.status, 200);
  EXPECT_EQ(acc, "foobarbaz");
  EXPECT_TRUE(r.body.empty()) << "body drained via callback";
}

TEST(HttpClientStreamingTest, Callback_TotalBytesMatchContentLength) {
  MockHttpClient http;
  sso::HttpResponse canned;
  canned.status = 200;
  http.responses["https://x/y"] = canned;
  http.streamingChunks = {"hello", " ", "world"};

  std::size_t total = 0;
  http.get_streaming("https://x/y", {},
                     [&](const char *, std::size_t n) { total += n; });
  EXPECT_EQ(total, 11u);
}

TEST(HttpClientStreamingTest, Callback_AbortsOnException) {
  MockHttpClient http;
  sso::HttpResponse canned;
  canned.status = 200;
  http.responses["https://x/y"] = canned;
  http.streamingChunks = {"first", "second", "third"};

  int calls = 0;
  EXPECT_THROW({
    http.get_streaming(
        "https://x/y", {},
        [&](const char *, std::size_t) {
          ++calls;
          if (calls == 2) throw std::runtime_error("stop");
        });
  }, std::runtime_error);
  // Mock stops delivering further chunks once the callback throws.
  EXPECT_EQ(calls, 2);
}
