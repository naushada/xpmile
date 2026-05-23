#include "sign_jwt_on_prem.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

#include "json.hpp"
#include "mongodbc.hpp"

namespace agent {

namespace {

// ── OpenSSL RAII handles ─────────────────────────────────────────────────────

struct EvpPkeyDeleter {
  void operator()(EVP_PKEY *p) const noexcept { EVP_PKEY_free(p); }
};
using EvpPkeyPtr = std::unique_ptr<EVP_PKEY, EvpPkeyDeleter>;

struct BioDeleter {
  void operator()(BIO *b) const noexcept { BIO_free_all(b); }
};
using BioPtr = std::unique_ptr<BIO, BioDeleter>;

struct EvpMdCtxDeleter {
  void operator()(EVP_MD_CTX *c) const noexcept { EVP_MD_CTX_free(c); }
};
using EvpMdCtxPtr = std::unique_ptr<EVP_MD_CTX, EvpMdCtxDeleter>;

// Load an RSA private key from PEM bytes.
EvpPkeyPtr load_private_key(const std::string &pem) {
  BioPtr bio{BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()))};
  if (!bio) return nullptr;
  EVP_PKEY *raw = PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr);
  return EvpPkeyPtr{raw};
}

// Sign @p to_sign with SHA-256 + RSA-PKCS#1 v1.5 (i.e. RS256).
bool rs256_sign(EVP_PKEY *pkey,
                const std::vector<std::uint8_t> &to_sign,
                std::vector<std::uint8_t> &out) {
  EvpMdCtxPtr ctx{EVP_MD_CTX_new()};
  if (!ctx) return false;
  if (1 != EVP_DigestSignInit(ctx.get(), nullptr, EVP_sha256(),
                               nullptr, pkey)) {
    return false;
  }
  if (1 != EVP_DigestSignUpdate(ctx.get(),
                                 to_sign.data(), to_sign.size())) {
    return false;
  }
  std::size_t siglen = 0;
  if (1 != EVP_DigestSignFinal(ctx.get(), nullptr, &siglen)) return false;
  out.resize(siglen);
  if (1 != EVP_DigestSignFinal(ctx.get(), out.data(), &siglen)) return false;
  out.resize(siglen);  // the final call may return a smaller actual size
  return true;
}

} // namespace

void sign_jwt_on_prem(IMongodbClient &db,
                      const dbproto::DbRequest &req,
                      dbproto::DbResponse &rsp) {
  // The dispatcher in wsdbagent.cpp has already populated rsp.reqid.
  rsp.ok = false;

  // sval carries the kid_hint: "current" → active key, else the literal kid.
  // coll is "idp_signing_keys" (set by the cloud caller; we trust it).
  std::string query;
  if (req.sval == "current" || req.sval.empty()) {
    query = R"({"active":true})";
  } else {
    query = std::string(R"({"_id":")") + req.sval + R"("})";
  }
  const std::string projection =
      R"({"_id":1,"privateKeyPem":1,"alg":1})";

  const std::string doc_json = db.get_document(req.coll, query, projection);
  if (doc_json.empty()) {
    rsp.errmsg = std::string("no signing key matches kid='") + req.sval + "'";
    return;
  }

  nlohmann::json doc;
  try {
    doc = nlohmann::json::parse(doc_json);
  } catch (const std::exception &e) {
    rsp.errmsg = std::string("could not parse signing-key doc: ") + e.what();
    return;
  }

  const std::string kid = doc.value("_id", std::string{});
  const std::string pem = doc.value("privateKeyPem", std::string{});
  const std::string alg = doc.value("alg", std::string{"RS256"});

  if (alg != "RS256") {
    rsp.errmsg = std::string("stored signing key alg is ") + alg
               + " (only RS256 supported)";
    return;
  }
  if (pem.empty()) {
    rsp.errmsg = "signing key has no privateKeyPem";
    return;
  }

  EvpPkeyPtr pkey = load_private_key(pem);
  if (!pkey) {
    rsp.errmsg = "failed to parse signing key PEM";
    return;
  }

  std::vector<std::uint8_t> signature;
  if (!rs256_sign(pkey.get(), req.doc, signature)) {
    rsp.errmsg = "RS256 sign failed";
    return;
  }

  rsp.ok   = true;
  rsp.sval = kid;                  // resolved kid actually used
  rsp.data = std::move(signature); // raw signature bytes (cloud base64url-encodes)
}

} // namespace agent
