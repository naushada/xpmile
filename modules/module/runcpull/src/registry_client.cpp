#include "registry_client.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>
#include <sys/utsname.h>

#include "json.hpp"
#include "sso_http_client.hpp"

namespace runcpull {

namespace {

// Percent-encode a string per RFC 3986 unreserved set. Local copy because
// sso::encode_form takes a map; we just need a single-string encoder.
std::string pct_encode(std::string_view s) {
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

bool is_hub_host(std::string_view host) {
  return host == "docker.io" || host == "registry-1.docker.io"
      || host == "registry.docker.io" || host == "index.docker.io";
}

// Validate that an image name part is composed of [a-z0-9._-]. We're more
// permissive than the strict Docker grammar but reject obvious garbage.
bool valid_name_component(std::string_view part) {
  if (part.empty()) return false;
  for (char c : part) {
    if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-'
          || c == '.')) {
      return false;
    }
  }
  return true;
}

}  // namespace

// ── parse_ref ────────────────────────────────────────────────────────────

RefParseResult parse_ref(std::string_view input) {
  RefParseResult out;
  if (input.empty()) { out.error = RefError::EMPTY; return out; }

  std::string s(input);

  // Strip digest first — it must follow the name, possibly with a tag in
  // between. After this, `body` is "<host?>/<name>[:tag]".
  std::string body = s;
  std::string digest;
  const std::size_t at = s.find('@');
  if (at != std::string::npos) {
    digest = s.substr(at + 1);
    if (digest.find("sha256:") != 0 && digest.find("sha512:") != 0) {
      // Accept any algorithm prefix — Phase C's digest module enforces
      // sha256-only for the actual download verification.
    }
    body = s.substr(0, at);
  }

  // Split host vs the rest. A leading segment with a '.' or ':' or "localhost"
  // is the registry host; otherwise the whole thing is the image name.
  std::string host;
  std::string rest;
  const std::size_t slash = body.find('/');
  if (slash != std::string::npos) {
    std::string first = body.substr(0, slash);
    if (first.find('.') != std::string::npos
        || first.find(':') != std::string::npos
        || first == "localhost") {
      host = first;
      rest = body.substr(slash + 1);
    } else {
      rest = body;
    }
  } else {
    rest = body;
  }

  if (host.empty()) {
    host = "docker.io";
  } else if (!is_hub_host(host)) {
    out.error = RefError::NON_HUB_REGISTRY;
    return out;
  } else {
    // Normalise alternate Hub spellings.
    host = "docker.io";
  }

  // Split tag from the remaining "name[:tag]". A digest already prevents
  // an at-sign appearing here; the colon separates tag.
  std::string tag;
  std::string name = rest;
  const std::size_t colon = rest.rfind(':');
  if (colon != std::string::npos && rest.find('/', colon) == std::string::npos) {
    tag  = rest.substr(colon + 1);
    name = rest.substr(0, colon);
  }

  // Single-component name without a slash → library/<name>.
  if (name.find('/') == std::string::npos) {
    name = "library/" + name;
  }

  // Validate name components.
  std::size_t pos = 0;
  while (pos < name.size()) {
    std::size_t sl = name.find('/', pos);
    std::string part = name.substr(pos, sl == std::string::npos
                                            ? std::string::npos
                                            : sl - pos);
    if (!valid_name_component(part)) {
      out.error = RefError::INVALID_CHARS;
      return out;
    }
    if (sl == std::string::npos) break;
    pos = sl + 1;
  }

  // When both a tag and a digest are present, digest wins.
  if (!digest.empty()) tag.clear();
  // Default tag.
  if (digest.empty() && tag.empty()) tag = "latest";

  out.ref.host   = std::move(host);
  out.ref.name   = std::move(name);
  out.ref.tag    = std::move(tag);
  out.ref.digest = std::move(digest);
  return out;
}

// ── parse_www_authenticate ───────────────────────────────────────────────

namespace {

// Strip leading whitespace and a single layer of surrounding quotes.
std::string strip_quotes(std::string_view v) {
  std::size_t i = 0;
  while (i < v.size() && (v[i] == ' ' || v[i] == '\t')) ++i;
  std::size_t j = v.size();
  while (j > i && (v[j - 1] == ' ' || v[j - 1] == '\t')) --j;
  if (j > i + 1 && v[i] == '"' && v[j - 1] == '"') { ++i; --j; }
  return std::string(v.substr(i, j - i));
}

}  // namespace

AuthChallengeParse parse_www_authenticate(std::string_view header_value) {
  AuthChallengeParse out;
  // Expect "Bearer <directives>". Case-insensitive on the scheme.
  std::size_t i = 0;
  while (i < header_value.size() && std::isspace(static_cast<unsigned char>(header_value[i]))) ++i;
  if (header_value.substr(i, 6) != "Bearer" && header_value.substr(i, 6) != "bearer") {
    return out;
  }
  i += 6;
  while (i < header_value.size() && std::isspace(static_cast<unsigned char>(header_value[i]))) ++i;

  // Walk comma-separated key=value pairs.
  while (i < header_value.size()) {
    std::size_t eq = header_value.find('=', i);
    if (eq == std::string_view::npos) break;
    std::string_view key = header_value.substr(i, eq - i);

    std::size_t vstart = eq + 1;
    std::size_t vend;
    if (vstart < header_value.size() && header_value[vstart] == '"') {
      // Find matching closing quote, respecting nothing fancier than literal.
      vend = header_value.find('"', vstart + 1);
      if (vend == std::string_view::npos) break;
      ++vend;  // include the closing quote
    } else {
      vend = header_value.find(',', vstart);
      if (vend == std::string_view::npos) vend = header_value.size();
    }
    std::string_view raw_val = header_value.substr(vstart, vend - vstart);
    std::string      val     = strip_quotes(raw_val);

    // Trim whitespace from key.
    while (!key.empty() && (key.front() == ' ' || key.front() == '\t')) key.remove_prefix(1);
    while (!key.empty() && (key.back()  == ' ' || key.back()  == '\t')) key.remove_suffix(1);

    if (key == "realm")   out.value.realm   = val;
    else if (key == "service") out.value.service = val;
    // ignore other directives (scope, error, error_description)

    i = vend;
    while (i < header_value.size() && (header_value[i] == ',' || std::isspace(static_cast<unsigned char>(header_value[i])))) ++i;
  }

  out.ok = !out.value.realm.empty();
  return out;
}

// ── build_token_url ──────────────────────────────────────────────────────

std::string build_token_url(
    const std::string &realm,
    const std::string &service,
    const std::string &image_name,
    const std::string &action) {
  std::string url = realm;
  url += (realm.find('?') == std::string::npos) ? '?' : '&';
  url += "service=";
  url += pct_encode(service);
  url += "&scope=";
  // Scope value is the literal string "repository:<image>:<action>" —
  // pct_encode the whole thing (the colons + the image's slash get
  // encoded too, which Docker Hub accepts).
  std::string scope = "repository:";
  scope += image_name;
  scope += ':';
  scope += action;
  url += pct_encode(scope);
  return url;
}

// ── parse_token_response ─────────────────────────────────────────────────

TokenParse parse_token_response(std::string_view json_body) {
  TokenParse out;
  try {
    auto j = nlohmann::json::parse(json_body);
    // Prefer "token" over "access_token" when both present.
    if (j.contains("token") && j["token"].is_string()) {
      out.token = j["token"].get<std::string>();
      out.ok   = !out.token.empty();
      return out;
    }
    if (j.contains("access_token") && j["access_token"].is_string()) {
      out.token = j["access_token"].get<std::string>();
      out.ok   = !out.token.empty();
      return out;
    }
  } catch (...) {
    // fall through
  }
  return out;
}

// ── Host arch ────────────────────────────────────────────────────────────

std::string HostProbe::uname_m() const {
  struct utsname u{};
  if (uname(&u) != 0) return "";
  return std::string(u.machine);
}

HostArchResult resolve_host_arch(const IHostProbe &probe) {
  HostArchResult out;
  const std::string m = probe.uname_m();
  if (m == "x86_64" || m == "amd64") {
    out.arch = HostArch::AMD64;
    out.name = "amd64";
  } else if (m == "aarch64" || m == "arm64") {
    out.arch = HostArch::ARM64;
    out.name = "arm64";
  } else if (m == "armv7l" || m == "armv7") {
    out.arch    = HostArch::ARMV7;
    out.name    = "arm";
    out.variant = "v7";
  } else {
    out.arch = HostArch::UNKNOWN;
    out.name = m;
  }
  return out;
}

HostArchResult resolve_host_arch_from_override(std::string_view flag) {
  HostArchResult out;
  std::string f(flag);
  std::transform(f.begin(), f.end(), f.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  if (f == "amd64" || f == "x86_64" || f == "x64") {
    out.arch = HostArch::AMD64;
    out.name = "amd64";
  } else if (f == "arm64" || f == "aarch64") {
    out.arch = HostArch::ARM64;
    out.name = "arm64";
  } else if (f == "armv7" || f == "armv7l" || f == "arm/v7") {
    out.arch    = HostArch::ARMV7;
    out.name    = "arm";
    out.variant = "v7";
  } else {
    out.arch = HostArch::UNKNOWN;
    out.name = std::string(flag);
  }
  return out;
}

// ── RegistryClient ───────────────────────────────────────────────────────

namespace {

constexpr const char *kRegistryHost   = "registry-1.docker.io";
constexpr const char *kAcceptManifest =
    "application/vnd.oci.image.index.v1+json, "
    "application/vnd.docker.distribution.manifest.list.v2+json, "
    "application/vnd.oci.image.manifest.v1+json, "
    "application/vnd.docker.distribution.manifest.v2+json";

}  // namespace

RegistryClient::RegistryClient(sso::IHttpClient &http, ImageRef image)
    : m_http(http), m_image(std::move(image)) {}

RegistryError RegistryClient::ensure_token() {
  if (!m_token.empty()) return RegistryError::NONE;

  // Probe /v2/ to harvest the Www-Authenticate challenge.
  const std::string probe_url =
      std::string("https://") + kRegistryHost + "/v2/";
  const sso::HttpResponse probe = m_http.get(probe_url);
  if (probe.status == 0) return RegistryError::TRANSPORT;
  if (probe.status >= 200 && probe.status < 300) {
    // Open registry — no token needed.
    ++m_token_fetches;
    return RegistryError::NONE;
  }
  if (probe.status != 401) return RegistryError::AUTH;

  // Read the Www-Authenticate header (case-insensitive — sso lowercases keys).
  std::string challenge;
  for (const auto &kv : probe.headers) {
    if (kv.first == "www-authenticate" || kv.first == "WWW-Authenticate") {
      challenge = kv.second;
      break;
    }
  }
  if (challenge.empty()) return RegistryError::MALFORMED_CHALLENGE;
  const AuthChallengeParse ch = parse_www_authenticate(challenge);
  if (!ch.ok) return RegistryError::MALFORMED_CHALLENGE;

  // Fetch the bearer.
  const std::string token_url =
      build_token_url(ch.value.realm, ch.value.service, m_image.name, "pull");
  const sso::HttpResponse tok = m_http.get(token_url);
  if (tok.status == 0) return RegistryError::TRANSPORT;
  if (tok.status < 200 || tok.status >= 300) return RegistryError::AUTH;
  const TokenParse t = parse_token_response(tok.body);
  if (!t.ok) return RegistryError::MALFORMED_TOKEN;

  m_token = t.token;
  ++m_token_fetches;
  return RegistryError::NONE;
}

ManifestFetch RegistryClient::fetch_manifest(const std::string &reference) {
  ManifestFetch out;
  const RegistryError ae = ensure_token();
  if (ae != RegistryError::NONE) { out.error = ae; return out; }

  const std::string url =
      std::string("https://") + kRegistryHost + "/v2/" + m_image.name
      + "/manifests/" + reference;

  std::map<std::string, std::string> headers;
  headers["Accept"] = kAcceptManifest;
  if (!m_token.empty()) headers["Authorization"] = "Bearer " + m_token;

  const sso::HttpResponse r = m_http.get(url, headers);
  if (r.status == 0) { out.error = RegistryError::TRANSPORT; return out; }
  if (r.status == 404) { out.error = RegistryError::MANIFEST_NOT_FOUND; return out; }
  if (r.status < 200 || r.status >= 300) {
    out.error = RegistryError::TRANSPORT;
    out.detail = "manifest http status " + std::to_string(r.status);
    return out;
  }
  out.body = r.body;
  for (const auto &kv : r.headers) {
    if (kv.first == "content-type" || kv.first == "Content-Type") {
      // Strip any "; charset=…" suffix.
      const auto semi = kv.second.find(';');
      out.media_type = (semi == std::string::npos) ? kv.second
                                                    : kv.second.substr(0, semi);
      // Trim trailing whitespace.
      while (!out.media_type.empty() && std::isspace(static_cast<unsigned char>(out.media_type.back()))) {
        out.media_type.pop_back();
      }
      break;
    }
  }
  return out;
}

RegistryError RegistryClient::fetch_blob(
    const std::string &digest,
    const sso::BodyChunkCallback &on_chunk) {
  const RegistryError ae = ensure_token();
  if (ae != RegistryError::NONE) return ae;

  std::string url =
      std::string("https://") + kRegistryHost + "/v2/" + m_image.name
      + "/blobs/" + digest;
  std::map<std::string, std::string> headers;
  if (!m_token.empty()) headers["Authorization"] = "Bearer " + m_token;

  constexpr int kMaxHops = 5;
  for (int hops = 0; hops < kMaxHops; ++hops) {
    const sso::HttpResponse r = m_http.get_streaming(url, headers, on_chunk);
    if (r.status == 0) return RegistryError::TRANSPORT;
    if (r.status == 404) return RegistryError::BLOB_NOT_FOUND;
    if (r.status >= 200 && r.status < 300) return RegistryError::NONE;

    // Look for Location and run it through redirect_step.
    std::string location;
    for (const auto &kv : r.headers) {
      if (kv.first == "location" || kv.first == "Location") {
        location = kv.second;
        break;
      }
    }
    const sso::RedirectStep step =
        sso::redirect_step(url, r.status, location, headers);
    if (!step.follow) {
      if (r.status >= 400) return RegistryError::TRANSPORT;
      return RegistryError::TRANSPORT;
    }
    url     = step.next_url;
    headers = step.headers;     // Authorization dropped on cross-origin hops
  }
  return RegistryError::REDIRECT_LIMIT;
}

}  // namespace runcpull
