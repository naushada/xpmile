#ifndef SSO_HTTP_CLIENT_HPP
#define SSO_HTTP_CLIENT_HPP

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
 */
namespace sso {

/// Result of an outbound HTTP request.
struct HttpResponse {
  long        status = 0;  ///< HTTP status code; 0 means a transport failure
  std::string body;

  bool ok() const { return status >= 200 && status < 300; }
};

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
};

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
  HttpResponse post_form(
      const std::string &url,
      const std::map<std::string, std::string> &fields) override;

private:
  HttpResponse request(const std::string &method, const std::string &url,
                        const std::string &body,
                        const std::string &content_type);

  int m_timeout_secs;
};

} // namespace sso

#endif // SSO_HTTP_CLIENT_HPP
