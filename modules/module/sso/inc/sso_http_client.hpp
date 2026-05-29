#ifndef SSO_HTTP_CLIENT_HPP
#define SSO_HTTP_CLIENT_HPP

#include <cstddef>
#include <functional>
#include <map>
#include <string>

/**
 * @file sso_http_client.hpp
 * @brief Outbound HTTPS client interface for the SSO feature
 *        (docs/design/sso/sso-design.md §6).
 *
 * SSO needs outbound calls — OIDC discovery, the token endpoint, JWKS. The
 * @ref IHttpClient interface is what every caller depends on; a libcurl-backed
 * implementation and a test mock both satisfy it. Decoupling this way keeps
 * the network dependency out of unit tests.
 *
 * The custom-headers `get()` overload, the streaming-callback `get()` overload,
 * and the redirect-policy free functions below are the Phase A extensions for
 * the in-house Docker Hub puller (docs/design/runc-pull/runc-pull-design.md).
 * Existing SSO callers keep their existing signatures; the additions are
 * additive and have default implementations on @ref IHttpClient so existing
 * mocks compile unchanged.
 */
namespace sso {

/// Result of an outbound HTTP request.
struct HttpResponse {
  long        status = 0;  ///< HTTP status code; 0 means a transport failure
  std::string body;
  std::map<std::string, std::string> headers;  ///< response headers (lowercased keys)

  bool ok() const { return status >= 200 && status < 300; }
};

/// Callback invoked for each chunk of a streaming body. `data` lives for the
/// duration of the call only; copy what you need.
using BodyChunkCallback = std::function<void(const char *data, std::size_t len)>;

/// Abstract outbound HTTP client — implemented by the libcurl client and by a
/// test mock.
class IHttpClient {
public:
  virtual ~IHttpClient() = default;

  /// Issue a GET request.
  virtual HttpResponse get(const std::string &url) = 0;

  /// Issue a POST with an application/x-www-form-urlencoded body built from
  /// @p fields.
  virtual HttpResponse post_form(
      const std::string &url,
      const std::map<std::string, std::string> &fields) = 0;

  /// Issue a GET request with custom request headers. Default impl drops the
  /// headers and forwards to the no-headers @ref get() — existing SSO callers
  /// + mocks keep their behaviour. Phase A registry callers (Docker Hub auth
  /// `Bearer` + multi-value `Accept`) override.
  virtual HttpResponse get(
      const std::string &url,
      const std::map<std::string, std::string> &headers) {
    (void)headers;
    return get(url);
  }

  /// Streaming GET — invoke @p on_chunk for each piece of the body as it
  /// arrives. Returned @ref HttpResponse has @c status set; @c body is left
  /// empty (caller drained it via the callback). Default impl falls back to
  /// the buffered @ref get() and delivers the body as one big chunk so older
  /// IHttpClient subclasses keep compiling. Phase A registry callers
  /// (downloading 100+ MB layer blobs) override to avoid buffering.
  virtual HttpResponse get_streaming(
      const std::string &url,
      const std::map<std::string, std::string> &headers,
      const BodyChunkCallback &on_chunk) {
    HttpResponse r = get(url, headers);
    if (on_chunk && !r.body.empty()) {
      on_chunk(r.body.data(), r.body.size());
      r.body.clear();
    }
    return r;
  }
};

/// Per-step result of redirect-following. Pure-function — no I/O.
struct RedirectStep {
  bool        follow = false;            ///< true iff the input is a 3xx with a valid Location
  std::string next_url;                  ///< resolved absolute URL (only set when follow == true)
  std::map<std::string, std::string> headers;  ///< headers to send on the next hop
  bool        downgrade_to_get = false;  ///< true for 303 — caller should drop request body
};

/**
 * @brief Compute the next URL + headers after a 3xx response. Pure function.
 *
 * - 301 / 302 / 303 / 307 / 308 → follow == true (303 also sets downgrade_to_get).
 * - All other statuses → follow == false.
 * - @p location_header is taken as absolute when it starts with `http://` or
 *   `https://`; otherwise it's resolved against @p current_url's
 *   scheme + host + (root-relative path).
 * - Cross-origin hops drop the `Authorization` header from @p headers_in
 *   (curl / browser policy — Docker Hub redirects blob GETs to a CDN that
 *   doesn't want the registry bearer). Same-origin hops keep all headers.
 */
RedirectStep redirect_step(
    const std::string &current_url,
    long status,
    const std::string &location_header,
    const std::map<std::string, std::string> &headers_in);

/**
 * @brief Whether two URLs share an origin (scheme + host + port).
 *
 * Default ports are treated as equivalent to their explicit form
 * (`https://x = https://x:443`, `http://x = http://x:80`). Case-insensitive on
 * scheme + host; path / query / fragment ignored.
 */
bool url_same_origin(const std::string &a, const std::string &b);

/**
 * @brief Encode form fields into an application/x-www-form-urlencoded body.
 *
 * Each key and value is percent-encoded (RFC 3986 unreserved set kept as-is),
 * pairs joined with '&'. Iteration is in sorted key order, so the output is
 * deterministic.
 */
std::string encode_form(const std::map<std::string, std::string> &fields);

/**
 * @brief Outbound HTTPS client — ACE_SOCK for the connect, OpenSSL for TLS.
 *
 * Verifies the server certificate chain against the system CA bundle and the
 * hostname, both during the TLS handshake. The networking path is not
 * unit-tested (see docs/design/sso/sso-design.md §6); unit tests use a mock
 * behind @ref IHttpClient.
 */
class HttpClient : public IHttpClient {
public:
  /// @param timeout_secs  connect and per-read timeout, in seconds.
  explicit HttpClient(int timeout_secs = 5);

  HttpResponse get(const std::string &url) override;
  HttpResponse get(
      const std::string &url,
      const std::map<std::string, std::string> &headers) override;
  HttpResponse post_form(
      const std::string &url,
      const std::map<std::string, std::string> &fields) override;

private:
  HttpResponse request(const std::string &method, const std::string &url,
                        const std::string &body,
                        const std::string &content_type,
                        const std::map<std::string, std::string> &extra_headers);

  int m_timeout_secs;
};

} // namespace sso

#endif // SSO_HTTP_CLIENT_HPP
