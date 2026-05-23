#include "saml_signature.hpp"

#include <mutex>

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/valid.h>

#include <xmlsec/xmlsec.h>
#include <xmlsec/xmldsig.h>
#include <xmlsec/xmltree.h>
#include <xmlsec/crypto.h>

namespace sso {

namespace {

const char *kSamlNs = "urn:oasis:names:tc:SAML:2.0:assertion";

const xmlChar *xc(const char *s) {
  return reinterpret_cast<const xmlChar *>(s);
}

bool elem_is(xmlNodePtr n, const char *ns, const char *local) {
  return n != nullptr && n->type == XML_ELEMENT_NODE &&
         xmlStrEqual(n->name, xc(local)) && n->ns != nullptr &&
         n->ns->href != nullptr && xmlStrEqual(n->ns->href, xc(ns));
}

int count_elements(xmlNodePtr node, const char *ns, const char *local) {
  int total = 0;
  for (xmlNodePtr c = node; c != nullptr; c = c->next) {
    if (c->type != XML_ELEMENT_NODE) continue;
    if (elem_is(c, ns, local)) ++total;
    total += count_elements(c->children, ns, local);
  }
  return total;
}

xmlNodePtr first_element(xmlNodePtr node, const char *ns, const char *local) {
  for (xmlNodePtr c = node; c != nullptr; c = c->next) {
    if (c->type != XML_ELEMENT_NODE) continue;
    if (elem_is(c, ns, local)) return c;
    if (xmlNodePtr found = first_element(c->children, ns, local))
      return found;
  }
  return nullptr;
}

// Register every ID-named attribute as an XML ID, so a DSig
// Reference URI="#id" resolves. Without this, even a valid signature fails.
void register_ids(xmlNodePtr node) {
  for (xmlNodePtr c = node; c != nullptr; c = c->next) {
    if (c->type != XML_ELEMENT_NODE) continue;
    xmlAttrPtr attr = xmlHasProp(c, xc("ID"));
    if (attr != nullptr) {
      xmlChar *val = xmlNodeListGetString(c->doc, attr->children, 1);
      if (val != nullptr) {
        xmlAddID(nullptr, c->doc, val, attr);
        xmlFree(val);
      }
    }
    register_ids(c->children);
  }
}

} // namespace

bool saml_crypto_init() {
  static std::once_flag once;
  static bool ready = false;
  std::call_once(once, [] {
    xmlInitParser();
    if (xmlSecInit() < 0) return;
    if (xmlSecCryptoAppInit(nullptr) < 0) return;
    if (xmlSecCryptoInit() < 0) return;
    ready = true;
  });
  return ready;
}

SamlSignatureResult verify_saml_signature(const std::string &xml,
                                          const std::string &idp_cert_pem) {
  SamlSignatureResult out;

  if (!saml_crypto_init()) {
    out.error = "xmlsec could not be initialised";
    return out;
  }

  xmlDocPtr doc = xmlReadMemory(
      xml.data(), static_cast<int>(xml.size()), "saml.xml", nullptr,
      XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
  if (doc == nullptr) {
    out.error = "SAML response XML is malformed";
    return out;
  }
  xmlNodePtr root = xmlDocGetRootElement(doc);
  register_ids(root);

  // Structural guards — a signature-wrapping decoy surfaces as an extra
  // <Signature> or an extra <Assertion>.
  const int sig_count = count_elements(
      root, reinterpret_cast<const char *>(xmlSecDSigNs), "Signature");
  const int assertion_count = count_elements(root, kSamlNs, "Assertion");
  if (sig_count == 0) {
    out.error = "SAML response is not signed";
    xmlFreeDoc(doc);
    return out;
  }
  if (sig_count != 1) {
    out.error = "SAML response carries more than one signature";
    xmlFreeDoc(doc);
    return out;
  }
  if (assertion_count != 1) {
    out.error = "SAML response must carry exactly one assertion";
    xmlFreeDoc(doc);
    return out;
  }

  xmlNodePtr sig = xmlSecFindNode(root, xmlSecNodeSignature, xmlSecDSigNs);
  xmlNodePtr assertion = first_element(root, kSamlNs, "Assertion");

  // The configured IdP certificate is the ONLY trusted verification key.
  xmlSecKeyPtr key = xmlSecCryptoAppKeyLoadMemory(
      reinterpret_cast<const xmlSecByte *>(idp_cert_pem.data()),
      idp_cert_pem.size(), xmlSecKeyDataFormatCertPem, nullptr, nullptr,
      nullptr);
  if (key == nullptr) {
    out.error = "could not load the IdP signing certificate";
    xmlFreeDoc(doc);
    return out;
  }

  xmlSecDSigCtxPtr dsig = xmlSecDSigCtxCreate(nullptr);
  if (dsig == nullptr) {
    xmlSecKeyDestroy(key);
    xmlFreeDoc(doc);
    out.error = "could not create the signature context";
    return out;
  }
  // With signKey pinned, xmlsec ignores any <KeyInfo> the response carries —
  // an attacker-supplied certificate is never trusted.
  dsig->signKey = key;

  const int rv = xmlSecDSigCtxVerify(dsig, sig);
  if (rv != 0 || dsig->status != xmlSecDSigStatusSucceeded) {
    out.error = "SAML signature verification failed";
    xmlSecDSigCtxDestroy(dsig);
    xmlFreeDoc(doc);
    return out;
  }

  // Signature-wrapping guard: the element the signature actually covers must
  // be the assertion we trust, or the response root that contains it.
  xmlNodePtr signed_info =
      xmlSecFindChild(sig, xmlSecNodeSignedInfo, xmlSecDSigNs);
  xmlNodePtr reference =
      signed_info != nullptr
          ? xmlSecFindChild(signed_info, xmlSecNodeReference, xmlSecDSigNs)
          : nullptr;
  std::string uri;
  if (reference != nullptr) {
    xmlChar *u = xmlGetProp(reference, xc("URI"));
    if (u != nullptr) {
      uri = reinterpret_cast<const char *>(u);
      xmlFree(u);
    }
  }

  xmlNodePtr signed_node = nullptr;
  if (uri.empty()) {
    signed_node = root;  // URI="" — the whole document
  } else if (uri[0] == '#') {
    xmlAttrPtr id_attr = xmlGetID(doc, xc(uri.c_str() + 1));
    signed_node = id_attr != nullptr ? id_attr->parent : nullptr;
  }

  xmlSecDSigCtxDestroy(dsig);

  if (signed_node != assertion && signed_node != root) {
    out.error = "the signature does not cover the assertion";
    xmlFreeDoc(doc);
    return out;
  }

  xmlFreeDoc(doc);
  out.ok = true;
  return out;
}

} // namespace sso
