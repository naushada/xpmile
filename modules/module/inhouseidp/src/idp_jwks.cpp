#include "idp_jwks.hpp"

#include <memory>
#include <string>
#include <vector>

#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

#include "json.hpp"
#include "mongodbc.hpp"
#include "sso_util.hpp"  // sso::base64url_encode

namespace idp {

namespace {

// ── OpenSSL RAII handles (match the style in sso_jwt.cpp) ────────────────────

struct EvpPkeyDeleter {
  void operator()(EVP_PKEY *p) const noexcept { EVP_PKEY_free(p); }
};
using EvpPkeyPtr = std::unique_ptr<EVP_PKEY, EvpPkeyDeleter>;

struct BioDeleter {
  void operator()(BIO *b) const noexcept { BIO_free_all(b); }
};
using BioPtr = std::unique_ptr<BIO, BioDeleter>;

/// Parse an RSA public key PEM into `(n_bytes, e_bytes)`.
/// Returns false on any parse/extract failure.
bool extract_rsa_ne(const std::string &pem,
                    std::vector<std::uint8_t> &out_n,
                    std::vector<std::uint8_t> &out_e) {
  BioPtr bio{BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()))};
  if (!bio) return false;
  EvpPkeyPtr pkey{PEM_read_bio_PUBKEY(bio.get(), nullptr, nullptr, nullptr)};
  if (!pkey) return false;

  // OpenSSL 1.1 path (matches sso_jwt.cpp). The RSA pointer is owned by the
  // EVP_PKEY; we must NOT free it.
  RSA *rsa = EVP_PKEY_get0_RSA(pkey.get());
  if (!rsa) return false;

  const BIGNUM *bn_n = nullptr;
  const BIGNUM *bn_e = nullptr;
  RSA_get0_key(rsa, &bn_n, &bn_e, nullptr);
  if (!bn_n || !bn_e) return false;

  out_n.resize(static_cast<std::size_t>(BN_num_bytes(bn_n)));
  out_e.resize(static_cast<std::size_t>(BN_num_bytes(bn_e)));
  BN_bn2bin(bn_n, out_n.data());
  BN_bn2bin(bn_e, out_e.data());
  return !out_n.empty() && !out_e.empty();
}

} // namespace

// ── jwks_from_keys ───────────────────────────────────────────────────────────

std::string jwks_from_keys(const std::vector<SigningKeyView> &keys,
                            std::int64_t now) {
  nlohmann::json out;
  out["keys"] = nlohmann::json::array();

  for (const auto &k : keys) {
    // Drop keys whose notAfter has elapsed. notAfter == 0 means "no expiry".
    if (k.notAfter != 0 && k.notAfter <= now) continue;
    if (k.kid.empty() || k.publicKeyPem.empty()) continue;
    if (k.alg != "RS256") continue;  // v1 advertises RS256 only

    std::vector<std::uint8_t> n_bytes, e_bytes;
    if (!extract_rsa_ne(k.publicKeyPem, n_bytes, e_bytes)) continue;

    nlohmann::json jwk = {
        {"kty", "RSA"},
        {"use", "sig"},
        {"alg", "RS256"},
        {"kid", k.kid},
        {"n",   sso::base64url_encode(n_bytes.data(), n_bytes.size())},
        {"e",   sso::base64url_encode(e_bytes.data(), e_bytes.size())},
    };
    out["keys"].push_back(std::move(jwk));
  }

  return out.dump();
}

// ── handle_idp_jwks_GET ──────────────────────────────────────────────────────

namespace {

std::vector<SigningKeyView> load_keys_from_collection(IMongodbClient &db) {
  // Project only fields we need; never read privateKeyPem here — JWKS is
  // public-only.
  const std::string projection =
      R"({"_id":1,"alg":1,"publicKeyPem":1,"notAfter":1})";
  const std::string list = db.get_documents("idp_signing_keys",
                                              /*query=*/std::string{},
                                              projection);
  std::vector<SigningKeyView> out;
  if (list.empty()) return out;

  nlohmann::json arr;
  try {
    arr = nlohmann::json::parse(list);
  } catch (const std::exception &) {
    return out;
  }
  if (!arr.is_array()) return out;

  for (const auto &row : arr) {
    if (!row.is_object()) continue;
    SigningKeyView v;
    v.kid          = row.value("_id", std::string{});
    v.alg          = row.value("alg", std::string{"RS256"});
    v.publicKeyPem = row.value("publicKeyPem", std::string{});
    // notAfter may be a Mongo Extended JSON date object or a plain epoch.
    if (row.contains("notAfter")) {
      const auto &na = row["notAfter"];
      if (na.is_number_integer())       v.notAfter = na.get<std::int64_t>();
      else if (na.is_object() && na.contains("$date")) {
        // Canonical Extended JSON {"$date":{"$numberLong":"..."}} OR
        // legacy {"$date":<int>}; handle both.
        const auto &d = na["$date"];
        if (d.is_number_integer())  v.notAfter = d.get<std::int64_t>() / 1000;
        else if (d.is_object() && d.contains("$numberLong"))
          v.notAfter = std::stoll(d["$numberLong"].get<std::string>()) / 1000;
      }
    }
    out.push_back(std::move(v));
  }
  return out;
}

} // namespace

sso::SsoHttpResult handle_idp_jwks_GET(IMongodbClient &db, std::int64_t now) {
  sso::SsoHttpResult r;
  r.status       = 200;
  r.content_type = "application/json";
  r.body         = jwks_from_keys(load_keys_from_collection(db), now);
  // Cache-Control is set by the wire-layer adapter (out of scope for this
  // pure-function helper).
  return r;
}

} // namespace idp
