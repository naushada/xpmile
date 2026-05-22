#include "saml_provider.hpp"

#include <ctime>
#include <utility>

#include <zlib.h>

#include "json.hpp"
#include "mongodbc.hpp"
#include "saml_response.hpp"
#include "saml_signature.hpp"
#include "sso_util.hpp"

namespace sso {

namespace {

// Escape the five XML predefined entities for use in element text / attributes.
std::string xml_escape(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    switch (c) {
      case '&':  out += "&amp;";  break;
      case '<':  out += "&lt;";   break;
      case '>':  out += "&gt;";   break;
      case '"':  out += "&quot;"; break;
      case '\'': out += "&apos;"; break;
      default:   out += c;        break;
    }
  }
  return out;
}

// Percent-encode everything outside the RFC 3986 unreserved set.
std::string url_encode(const std::string &s) {
  static const char hex[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(s.size());
  for (unsigned char c : s) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' ||
        c == '~') {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 0x0F];
    }
  }
  return out;
}

// SAML timestamps are ISO 8601 UTC, e.g. 2026-05-21T12:00:00Z.
std::string iso8601(std::int64_t epoch) {
  std::time_t t = static_cast<std::time_t>(epoch);
  std::tm tm{};
  gmtime_r(&t, &tm);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return buf;
}

} // namespace

std::string saml_deflate(const std::string &data) {
  z_stream zs{};
  // windowBits = -15 → raw DEFLATE, no zlib header (SAML redirect binding).
  if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -15, 8,
                   Z_DEFAULT_STRATEGY) != Z_OK) {
    return {};
  }
  zs.next_in  = reinterpret_cast<Bytef *>(const_cast<char *>(data.data()));
  zs.avail_in = static_cast<uInt>(data.size());

  std::string out;
  char buf[4096];
  int ret;
  do {
    zs.next_out  = reinterpret_cast<Bytef *>(buf);
    zs.avail_out = sizeof(buf);
    ret = deflate(&zs, Z_FINISH);
    out.append(buf, sizeof(buf) - zs.avail_out);
  } while (ret == Z_OK);
  deflateEnd(&zs);
  return ret == Z_STREAM_END ? out : std::string{};
}

std::string saml_inflate(const std::string &data) {
  z_stream zs{};
  if (inflateInit2(&zs, -15) != Z_OK) {
    return {};
  }
  zs.next_in  = reinterpret_cast<Bytef *>(const_cast<char *>(data.data()));
  zs.avail_in = static_cast<uInt>(data.size());

  std::string out;
  char buf[4096];
  int ret;
  do {
    zs.next_out  = reinterpret_cast<Bytef *>(buf);
    zs.avail_out = sizeof(buf);
    ret = inflate(&zs, Z_NO_FLUSH);
    if (ret != Z_OK && ret != Z_STREAM_END) {
      inflateEnd(&zs);
      return {};
    }
    out.append(buf, sizeof(buf) - zs.avail_out);
  } while (ret != Z_STREAM_END);
  inflateEnd(&zs);
  return out;
}

SamlProvider::SamlProvider(ProviderConfig config, std::string public_base_url,
                           IMongodbClient &db, IClock &clock)
    : m_config(std::move(config)),
      m_public_base_url(std::move(public_base_url)),
      m_db(db),
      m_clock(clock) {}

AuthnRequest SamlProvider::begin_login(const std::string &return_to) {
  // SAML IDs are XML NCNames — must not start with a digit; the '_' prefix
  // keeps that true whatever random_token() produces.
  const std::string request_id = "_" + random_token(20);
  const std::string acs_url =
      m_public_base_url + "/api/v1/sso/callback/" + m_config.id;
  const std::int64_t now = m_clock.now_unix();

  const std::string xml =
      "<samlp:AuthnRequest "
      "xmlns:samlp=\"urn:oasis:names:tc:SAML:2.0:protocol\" "
      "xmlns:saml=\"urn:oasis:names:tc:SAML:2.0:assertion\" "
      "ID=\"" + request_id + "\" Version=\"2.0\" "
      "IssueInstant=\"" + iso8601(now) + "\" "
      "Destination=\"" + xml_escape(m_config.idp_sso_url) + "\" "
      "AssertionConsumerServiceURL=\"" + xml_escape(acs_url) + "\" "
      "ProtocolBinding=\"urn:oasis:names:tc:SAML:2.0:bindings:HTTP-POST\">"
      "<saml:Issuer>" + xml_escape(m_config.sp_entity_id) + "</saml:Issuer>"
      "</samlp:AuthnRequest>";

  // Persist the one-time login transaction, keyed by the AuthnRequest ID. The
  // Response's InResponseTo must match this id, consumed at the callback (§E.4).
  nlohmann::json txn;
  txn["_id"]       = request_id;
  txn["provider"]  = m_config.id;
  txn["returnTo"]  = return_to;
  txn["createdAt"] = now;
  m_db.create_document(m_db.get_database(), "sso_transactions", txn.dump());

  // HTTP-Redirect binding: raw-DEFLATE, base64, percent-encode into the query.
  const std::string saml_request =
      url_encode(base64_encode(saml_deflate(xml)));
  const char separator =
      m_config.idp_sso_url.find('?') == std::string::npos ? '?' : '&';

  AuthnRequest req;
  req.redirect_url = m_config.idp_sso_url + separator +
                     "SAMLRequest=" + saml_request +
                     "&RelayState=" + url_encode(request_id);
  req.transaction_id = request_id;
  return req;
}

IdentityClaims SamlProvider::handle_callback(const std::string &saml_response,
                                             const std::string &relay_state) {
  // RelayState is an unsigned transport parameter — an attacker can tamper
  // it. The assertion's own InResponseTo (inside the signed payload) is
  // authoritative, so RelayState is deliberately not trusted here.
  (void)relay_state;

  IdentityClaims out;
  if (saml_response.empty()) {
    out.error = "missing SAMLResponse";
    return out;
  }

  // 1. Verify the XML-DSig signature before trusting any content.
  const SamlSignatureResult sig = verify_saml_signature(
      base64_decode(saml_response), m_config.idp_signing_cert);
  if (!sig.ok) {
    out.error = "SAML signature rejected: " + sig.error;
    return out;
  }

  // 2. Parse the now-authenticated response.
  const SamlResponse parsed = parse_saml_response(saml_response);
  if (!parsed.ok) {
    out.error = parsed.error;
    return out;
  }

  // 3. Validate conditions and atomically consume the login transaction.
  SamlConditionExpect expect;
  expect.audience  = m_config.sp_entity_id;
  expect.recipient = m_public_base_url + "/api/v1/sso/callback/" + m_config.id;
  const SamlConditionResult cond = validate_saml_conditions(
      parsed.assertion, expect, m_db, m_clock.now_unix());
  if (!cond.ok) {
    out.error = cond.error;
    return out;
  }

  // 4. Map the verified assertion to identity claims.
  out.ok             = true;
  out.subject        = parsed.assertion.subject_name_id;
  out.email          = parsed.assertion.email;
  out.email_verified = !parsed.assertion.email.empty();  // IdP-asserted + signed
  out.display_name   = parsed.assertion.display_name;
  out.groups         = parsed.assertion.groups;
  out.return_to      = cond.return_to;
  return out;
}

} // namespace sso
