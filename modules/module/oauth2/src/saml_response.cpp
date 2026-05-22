#include "saml_response.hpp"

#include <cctype>
#include <cstdio>

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#include "json.hpp"
#include "mongodbc.hpp"
#include "sso_util.hpp"

namespace sso {

namespace {

const char *kSamlNs  = "urn:oasis:names:tc:SAML:2.0:assertion";
const char *kSamlpNs = "urn:oasis:names:tc:SAML:2.0:protocol";

const xmlChar *xc(const char *s) {
  return reinterpret_cast<const xmlChar *>(s);
}

// Content of a node, trimmed of surrounding whitespace.
std::string node_content(xmlNodePtr node) {
  if (node == nullptr) return {};
  xmlChar *raw = xmlNodeGetContent(node);
  std::string s = raw ? reinterpret_cast<const char *>(raw) : std::string{};
  if (raw) xmlFree(raw);
  const std::size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return {};
  const std::size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

// First node matching @p expr relative to @p base, or null.
xmlNodePtr xpath_node(xmlXPathContextPtr ctx, xmlNodePtr base,
                      const char *expr) {
  ctx->node = base;
  xmlXPathObjectPtr obj = xmlXPathEvalExpression(xc(expr), ctx);
  xmlNodePtr out = nullptr;
  if (obj && obj->nodesetval && obj->nodesetval->nodeNr > 0)
    out = obj->nodesetval->nodeTab[0];
  if (obj) xmlXPathFreeObject(obj);
  return out;
}

std::string xpath_string(xmlXPathContextPtr ctx, xmlNodePtr base,
                         const char *expr) {
  return node_content(xpath_node(ctx, base, expr));
}

std::vector<std::string> xpath_strings(xmlXPathContextPtr ctx, xmlNodePtr base,
                                       const char *expr) {
  ctx->node = base;
  xmlXPathObjectPtr obj = xmlXPathEvalExpression(xc(expr), ctx);
  std::vector<std::string> out;
  if (obj && obj->nodesetval) {
    for (int i = 0; i < obj->nodesetval->nodeNr; ++i)
      out.push_back(node_content(obj->nodesetval->nodeTab[i]));
  }
  if (obj) xmlXPathFreeObject(obj);
  return out;
}

std::string attr_value(xmlNodePtr node, const char *name) {
  if (node == nullptr) return {};
  xmlChar *raw = xmlGetProp(node, xc(name));
  std::string s = raw ? reinterpret_cast<const char *>(raw) : std::string{};
  if (raw) xmlFree(raw);
  return s;
}

std::string to_lower(std::string s) {
  for (char &c : s)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

void extract_attributes(xmlXPathContextPtr ctx, xmlNodePtr assertion,
                        SamlAssertion &out) {
  ctx->node = assertion;
  xmlXPathObjectPtr attrs = xmlXPathEvalExpression(
      xc("saml:AttributeStatement/saml:Attribute"), ctx);
  if (attrs == nullptr || attrs->nodesetval == nullptr) {
    if (attrs) xmlXPathFreeObject(attrs);
    return;
  }
  for (int i = 0; i < attrs->nodesetval->nodeNr; ++i) {
    xmlNodePtr attr = attrs->nodesetval->nodeTab[i];
    const std::string name = to_lower(attr_value(attr, "Name"));
    const std::vector<std::string> values =
        xpath_strings(ctx, attr, "saml:AttributeValue");
    if (values.empty()) continue;

    // IdP attribute names vary; match on intent rather than an exact name.
    if (name.find("email") != std::string::npos && out.email.empty()) {
      out.email = values.front();
    } else if (name.find("group") != std::string::npos ||
               name.find("role") != std::string::npos) {
      for (const auto &v : values) out.groups.push_back(v);
    } else if (name.find("name") != std::string::npos &&
               out.display_name.empty()) {
      out.display_name = values.front();
    }
  }
  xmlXPathFreeObject(attrs);
}

} // namespace

SamlResponse parse_saml_response(const std::string &base64_response) {
  SamlResponse out;

  out.xml = base64_decode(base64_response);
  if (out.xml.empty()) {
    out.error = "SAML response is empty or not valid base64";
    return out;
  }

  // XXE-safe parse: NONET blocks network fetches; the absence of NOENT /
  // DTDLOAD means no external-entity substitution (libxml2's safe default).
  xmlDocPtr doc = xmlReadMemory(
      out.xml.data(), static_cast<int>(out.xml.size()), "samlresponse.xml",
      nullptr, XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
  if (doc == nullptr) {
    out.error = "SAML response XML is malformed";
    return out;
  }

  xmlXPathContextPtr ctx = xmlXPathNewContext(doc);
  if (ctx == nullptr) {
    xmlFreeDoc(doc);
    out.error = "could not create an XPath context";
    return out;
  }
  xmlXPathRegisterNs(ctx, xc("saml"), xc(kSamlNs));
  xmlXPathRegisterNs(ctx, xc("samlp"), xc(kSamlpNs));

  xmlNodePtr assertion =
      xpath_node(ctx, xmlDocGetRootElement(doc), "//saml:Assertion");
  if (assertion == nullptr) {
    xmlXPathFreeContext(ctx);
    xmlFreeDoc(doc);
    out.error = "SAML response carries no assertion";
    return out;
  }

  SamlAssertion &a = out.assertion;
  a.id              = attr_value(assertion, "ID");
  a.issuer          = xpath_string(ctx, assertion, "saml:Issuer");
  a.subject_name_id = xpath_string(ctx, assertion, "saml:Subject/saml:NameID");
  a.audience        = xpath_string(
      ctx, assertion,
      "saml:Conditions/saml:AudienceRestriction/saml:Audience");
  a.not_before      = xpath_string(ctx, assertion, "saml:Conditions/@NotBefore");
  a.not_on_or_after =
      xpath_string(ctx, assertion, "saml:Conditions/@NotOnOrAfter");
  a.recipient = xpath_string(
      ctx, assertion,
      "saml:Subject/saml:SubjectConfirmation/saml:SubjectConfirmationData/"
      "@Recipient");
  a.in_response_to = xpath_string(
      ctx, assertion,
      "saml:Subject/saml:SubjectConfirmation/saml:SubjectConfirmationData/"
      "@InResponseTo");
  extract_attributes(ctx, assertion, a);

  xmlXPathFreeContext(ctx);
  xmlFreeDoc(doc);

  out.ok = true;
  return out;
}

namespace {

// Days since the Unix epoch for a civil (proleptic Gregorian) date.
// Howard Hinnant's algorithm — avoids any libc date function.
std::int64_t days_from_civil(int y, int m, int d) {
  y -= (m <= 2);
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy =
      (153u * static_cast<unsigned>(m + (m > 2 ? -3 : 9)) + 2u) / 5u +
      static_cast<unsigned>(d) - 1u;
  const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
  return static_cast<std::int64_t>(era) * 146097 +
         static_cast<std::int64_t>(doe) - 719468;
}

// Parse a SAML timestamp (ISO 8601 UTC, e.g. 2026-05-21T12:05:00Z) to epoch
// seconds. Any fractional seconds / trailing "Z" are ignored.
bool parse_iso8601(const std::string &s, std::int64_t &out) {
  int y = 0, mon = 0, d = 0, h = 0, mi = 0, se = 0;
  if (std::sscanf(s.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d", &y, &mon, &d, &h, &mi,
                  &se) != 6) {
    return false;
  }
  out = days_from_civil(y, mon, d) * 86400 +
        static_cast<std::int64_t>(h) * 3600 + mi * 60 + se;
  return true;
}

} // namespace

SamlConditionResult validate_saml_conditions(const SamlAssertion &a,
                                             const SamlConditionExpect &expect,
                                             IMongodbClient &db,
                                             std::int64_t now) {
  SamlConditionResult out;
  constexpr std::int64_t kSkew = 60;  // clock-skew tolerance, seconds

  if (a.audience != expect.audience) {
    out.error = "SAML audience does not match this service provider";
    return out;
  }

  std::int64_t t = 0;
  if (!a.not_before.empty()) {
    if (!parse_iso8601(a.not_before, t)) {
      out.error = "SAML Conditions NotBefore is unparseable";
      return out;
    }
    if (now + kSkew < t) {
      out.error = "SAML assertion is not yet valid";
      return out;
    }
  }
  if (!a.not_on_or_after.empty()) {
    if (!parse_iso8601(a.not_on_or_after, t)) {
      out.error = "SAML Conditions NotOnOrAfter is unparseable";
      return out;
    }
    if (now - kSkew >= t) {
      out.error = "SAML assertion has expired";
      return out;
    }
  }

  if (a.recipient != expect.recipient) {
    out.error = "SAML SubjectConfirmationData Recipient mismatch";
    return out;
  }

  if (a.in_response_to.empty()) {
    out.error = "unsolicited SAML response rejected — no InResponseTo";
    return out;
  }

  // Look up the login transaction this assertion answers.
  nlohmann::json query;
  query["_id"] = a.in_response_to;
  const std::string raw =
      db.get_document("sso_transactions", query.dump(), "{}");
  if (raw.empty()) {
    out.error = "SAML InResponseTo matches no known login request";
    return out;
  }
  nlohmann::json txn =
      nlohmann::json::parse(raw, nullptr, /*allow_exceptions=*/false);
  if (txn.is_discarded() || !txn.is_object()) {
    out.error = "corrupt SAML transaction";
    return out;
  }

  // Atomically claim it — single-use. The guarded update matches only while
  // `consumed` is absent, so a replayed assertion fails here.
  nlohmann::json claim_filter;
  claim_filter["_id"]                 = a.in_response_to;
  claim_filter["consumed"]["$exists"] = false;
  nlohmann::json claim_update;
  claim_update["$set"]["consumed"]    = true;
  if (!db.update_collection("sso_transactions", claim_filter.dump(),
                            claim_update.dump())) {
    out.error = "SAML assertion has already been used";
    return out;
  }

  out.ok        = true;
  out.return_to = txn.value("returnTo", std::string{});
  return out;
}

} // namespace sso
