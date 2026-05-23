#include "sso_http_client.hpp"

#include <cctype>
#include <cstdlib>
#include <memory>

#include <sys/socket.h>
#include <sys/time.h>

#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#include "ace/INET_Addr.h"
#include "ace/SOCK_Connector.h"
#include "ace/SOCK_Stream.h"
#include "ace/Time_Value.h"

#include "http_parser.hpp"

namespace sso {

namespace {

// ── form encoding ──────────────────────────────────────────────────────────

// Percent-encode a string, keeping the RFC 3986 unreserved set unchanged.
std::string pct_encode(const std::string &s) {
  static const char hex[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(s.size());
  for (unsigned char c : s) {
    if (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~') {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 0x0F];
    }
  }
  return out;
}

// ── OpenSSL RAII ───────────────────────────────────────────────────────────

struct SslCtxDeleter {
  void operator()(SSL_CTX *p) const noexcept { SSL_CTX_free(p); }
};
struct SslDeleter {
  void operator()(SSL *p) const noexcept { SSL_free(p); }
};

// ── URL parsing ────────────────────────────────────────────────────────────

struct ParsedUrl {
  bool          ok = false;
  std::string   host;
  std::uint16_t port = 443;
  std::string   path = "/";
};

// Parse an https:// URL. Only HTTPS is accepted — every IdP endpoint is HTTPS.
ParsedUrl parse_https_url(const std::string &url) {
  ParsedUrl p;
  const std::string scheme = "https://";
  if (url.rfind(scheme, 0) != 0) return p;

  const std::string rest = url.substr(scheme.size());
  const std::size_t slash = rest.find('/');
  const std::string authority =
      (slash == std::string::npos) ? rest : rest.substr(0, slash);
  p.path = (slash == std::string::npos) ? "/" : rest.substr(slash);

  const std::size_t colon = authority.find(':');
  if (colon == std::string::npos) {
    p.host = authority;
  } else {
    p.host = authority.substr(0, colon);
    p.port = static_cast<std::uint16_t>(
        std::atoi(authority.substr(colon + 1).c_str()));
  }
  if (p.host.empty() || p.port == 0) return p;
  p.ok = true;
  return p;
}

} // namespace

std::string encode_form(const std::map<std::string, std::string> &fields) {
  std::string out;
  for (const auto &kv : fields) {
    if (!out.empty()) out += '&';
    out += pct_encode(kv.first);
    out += '=';
    out += pct_encode(kv.second);
  }
  return out;
}

HttpClient::HttpClient(int timeout_secs) : m_timeout_secs(timeout_secs) {}

HttpResponse HttpClient::get(const std::string &url) {
  return request("GET", url, "", "");
}

HttpResponse HttpClient::post_form(
    const std::string &url, const std::map<std::string, std::string> &fields) {
  return request("POST", url, encode_form(fields),
                 "application/x-www-form-urlencoded");
}

HttpResponse HttpClient::request(const std::string &method,
                                 const std::string &url,
                                 const std::string &body,
                                 const std::string &content_type) {
  HttpResponse resp;  // status 0 by default == transport failure

  const ParsedUrl u = parse_https_url(url);
  if (!u.ok) return resp;

  // ── DNS + TCP connect (with a connect timeout) ──
  ACE_INET_Addr addr;
  if (addr.set(u.port, u.host.c_str()) != 0) return resp;

  ACE_SOCK_Stream    stream;
  ACE_SOCK_Connector connector;
  ACE_Time_Value     timeout(m_timeout_secs);
  if (connector.connect(stream, addr, &timeout) == -1) return resp;

  // ACE sockets are not RAII for the handle — close it on every exit path.
  struct StreamGuard {
    ACE_SOCK_Stream &s;
    ~StreamGuard() { s.close(); }
  } stream_guard{stream};

  // Bound the TLS handshake and every read/write by the timeout.
  struct timeval tv;
  tv.tv_sec  = m_timeout_secs;
  tv.tv_usec = 0;
  (void)::setsockopt(stream.get_handle(), SOL_SOCKET, SO_RCVTIMEO, &tv,
                     sizeof(tv));
  (void)::setsockopt(stream.get_handle(), SOL_SOCKET, SO_SNDTIMEO, &tv,
                     sizeof(tv));

  // ── TLS — per-connection context, verified against the system CA bundle ──
  std::unique_ptr<SSL_CTX, SslCtxDeleter> ctx(SSL_CTX_new(TLS_client_method()));
  if (!ctx) return resp;
  SSL_CTX_set_min_proto_version(ctx.get(), TLS1_2_VERSION);
  SSL_CTX_set_verify(ctx.get(), SSL_VERIFY_PEER, nullptr);
  if (SSL_CTX_set_default_verify_paths(ctx.get()) != 1) return resp;

  std::unique_ptr<SSL, SslDeleter> ssl(SSL_new(ctx.get()));
  if (!ssl) return resp;
  SSL_set_fd(ssl.get(), static_cast<int>(stream.get_handle()));
  (void)SSL_set_tlsext_host_name(ssl.get(), u.host.c_str());  // SNI

  // Verify the certificate hostname during the handshake.
  X509_VERIFY_PARAM *vp = SSL_get0_param(ssl.get());
  X509_VERIFY_PARAM_set_hostflags(vp, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
  if (X509_VERIFY_PARAM_set1_host(vp, u.host.c_str(), u.host.size()) != 1)
    return resp;

  if (SSL_connect(ssl.get()) != 1)
    return resp;  // chain or hostname verification failed

  // ── send the request ──
  std::string req = method + " " + u.path + " HTTP/1.1\r\n";
  req += "Host: " + u.host + "\r\n";
  req += "Connection: close\r\n";
  req += "Accept: application/json\r\n";
  if (method == "POST") {
    req += "Content-Type: " + content_type + "\r\n";
    req += "Content-Length: " + std::to_string(body.size()) + "\r\n";
  }
  req += "\r\n";
  req += body;

  std::size_t sent = 0;
  while (sent < req.size()) {
    const int n = SSL_write(ssl.get(), req.data() + sent,
                            static_cast<int>(req.size() - sent));
    if (n <= 0) return resp;  // write failed / timed out
    sent += static_cast<std::size_t>(n);
  }

  // ── read the response to EOF (Connection: close) ──
  std::string raw;
  char buf[4096];
  for (;;) {
    const int n = SSL_read(ssl.get(), buf, sizeof(buf));
    if (n <= 0) break;  // 0 / <0 → peer closed, or read timed out
    raw.append(buf, static_cast<std::size_t>(n));
  }
  if (raw.empty()) return resp;

  // Hand the raw response to the project's HTTP parser, which already decodes
  // the response status line, headers, chunked transfer-encoding, and
  // gzip/deflate — no need to reimplement any of that here.
  Http parsed(raw);
  resp.status = parsed.status();
  resp.body   = parsed.body();
  return resp;
}

} // namespace sso
