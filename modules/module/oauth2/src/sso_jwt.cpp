#include "sso_jwt.hpp"

#include <openssl/bn.h>
#include <openssl/rsa.h>

#include "json.hpp"
#include "sso_util.hpp"

namespace sso {

namespace {

// Skew tolerance for exp / iat checks, in seconds.
constexpr std::int64_t kClockSkewSecs = 60;

// Build an RSA public-key EVP_PKEY from raw big-endian modulus/exponent bytes.
EvpPkeyPtr rsa_pubkey_from_ne(const std::string &n, const std::string &e) {
  if (n.empty() || e.empty()) return nullptr;

  BIGNUM *bn_n = BN_bin2bn(reinterpret_cast<const unsigned char *>(n.data()),
                           static_cast<int>(n.size()), nullptr);
  BIGNUM *bn_e = BN_bin2bn(reinterpret_cast<const unsigned char *>(e.data()),
                           static_cast<int>(e.size()), nullptr);
  RSA *rsa = RSA_new();
  if (!bn_n || !bn_e || !rsa) {
    BN_free(bn_n);
    BN_free(bn_e);
    RSA_free(rsa);
    return nullptr;
  }
  // RSA_set0_key takes ownership of bn_n / bn_e on success.
  if (RSA_set0_key(rsa, bn_n, bn_e, nullptr) != 1) {
    BN_free(bn_n);
    BN_free(bn_e);
    RSA_free(rsa);
    return nullptr;
  }

  EVP_PKEY *pkey = EVP_PKEY_new();
  if (!pkey || EVP_PKEY_assign_RSA(pkey, rsa) != 1) {
    EVP_PKEY_free(pkey);
    RSA_free(rsa);
    return nullptr;
  }
  // EVP_PKEY_assign_RSA took ownership of rsa.
  return EvpPkeyPtr(pkey);
}

// Verify an RSASSA-PKCS1-v1.5 + SHA-256 signature (i.e. RS256).
bool rsa_sha256_verify(EVP_PKEY *key, const std::string &data,
                       const std::string &sig) {
  EVP_MD_CTX *ctx = EVP_MD_CTX_new();
  if (!ctx) return false;
  bool ok = false;
  if (EVP_DigestVerifyInit(ctx, nullptr, EVP_sha256(), nullptr, key) == 1 &&
      EVP_DigestVerifyUpdate(ctx, data.data(), data.size()) == 1 &&
      EVP_DigestVerifyFinal(
          ctx, reinterpret_cast<const unsigned char *>(sig.data()),
          sig.size()) == 1) {
    ok = true;
  }
  EVP_MD_CTX_free(ctx);
  return ok;
}

// `aud` may be a string or an array; it must contain client_id. When more
// than one audience is present, `azp` must equal client_id (OIDC §3.1.3.7).
bool audience_ok(const nlohmann::json &claims, const std::string &client_id) {
  auto it = claims.find("aud");
  if (it == claims.end()) return false;

  if (it->is_string()) return it->get<std::string>() == client_id;

  if (it->is_array()) {
    bool found = false;
    for (const auto &a : *it)
      if (a.is_string() && a.get<std::string>() == client_id) found = true;
    if (!found) return false;
    if (it->size() > 1)
      return claims.value("azp", std::string{}) == client_id;
    return true;
  }
  return false;
}

JwtResult fail(const std::string &why) {
  JwtResult r;
  r.error = why;
  return r;
}

} // namespace

bool Jwks::parse(const std::string &jwks_json) {
  m_keys.clear();

  nlohmann::json j =
      nlohmann::json::parse(jwks_json, nullptr, /*allow_exceptions=*/false);
  if (j.is_discarded() || !j.is_object()) return false;

  auto keys = j.find("keys");
  if (keys == j.end() || !keys->is_array()) return false;

  for (const auto &k : *keys) {
    if (!k.is_object()) continue;
    if (k.value("kty", std::string{}) != "RSA") continue;  // RS256 → RSA only
    const std::string kid = k.value("kid", std::string{});
    if (kid.empty()) continue;

    EvpPkeyPtr key =
        rsa_pubkey_from_ne(base64url_decode(k.value("n", std::string{})),
                           base64url_decode(k.value("e", std::string{})));
    if (key) m_keys[kid] = std::move(key);
  }
  return true;
}

bool Jwks::has_key(const std::string &kid) const {
  return m_keys.find(kid) != m_keys.end();
}

EVP_PKEY *Jwks::get(const std::string &kid) const {
  auto it = m_keys.find(kid);
  return (it != m_keys.end()) ? it->second.get() : nullptr;
}

JwtResult verify_jwt(const std::string &token, const Jwks &jwks,
                     const JwtExpect &expect, std::int64_t clock_now) {
  // ── split into header.payload.signature ──
  const std::size_t dot1 = token.find('.');
  const std::size_t dot2 =
      (dot1 == std::string::npos) ? std::string::npos
                                  : token.find('.', dot1 + 1);
  if (dot1 == std::string::npos || dot2 == std::string::npos ||
      token.find('.', dot2 + 1) != std::string::npos) {
    return fail("malformed token");
  }
  const std::string h_b64 = token.substr(0, dot1);
  const std::string p_b64 = token.substr(dot1 + 1, dot2 - dot1 - 1);
  const std::string s_b64 = token.substr(dot2 + 1);

  // ── header: strict alg + kid ──
  nlohmann::json header = nlohmann::json::parse(
      base64url_decode(h_b64), nullptr, /*allow_exceptions=*/false);
  if (header.is_discarded() || !header.is_object())
    return fail("bad header");
  // RS256 only. Rejecting every other alg here is what makes algorithm
  // confusion (alg=none, HS256-with-the-public-key) structurally impossible.
  if (header.value("alg", std::string{}) != "RS256")
    return fail("unsupported alg (RS256 required)");
  const std::string kid = header.value("kid", std::string{});
  if (kid.empty()) return fail("missing kid");

  EVP_PKEY *key = jwks.get(kid);
  if (!key) return fail("unknown kid");

  // ── signature over "<header>.<payload>" ──
  const std::string sig = base64url_decode(s_b64);
  if (sig.empty()) return fail("bad signature encoding");
  if (!rsa_sha256_verify(key, h_b64 + "." + p_b64, sig))
    return fail("signature verification failed");

  // ── payload claims ──
  nlohmann::json claims = nlohmann::json::parse(
      base64url_decode(p_b64), nullptr, /*allow_exceptions=*/false);
  if (claims.is_discarded() || !claims.is_object())
    return fail("bad payload");

  if (claims.value("iss", std::string{}) != expect.issuer)
    return fail("issuer mismatch");
  if (!audience_ok(claims, expect.client_id))
    return fail("audience mismatch");

  const std::int64_t exp = claims.value("exp", std::int64_t{0});
  if (exp == 0 || exp + kClockSkewSecs <= clock_now)
    return fail("token expired");
  const std::int64_t iat = claims.value("iat", std::int64_t{0});
  if (iat != 0 && iat - kClockSkewSecs > clock_now)
    return fail("iat is in the future");

  if (claims.value("nonce", std::string{}) != expect.nonce)
    return fail("nonce mismatch");

  // ── success — extract identity claims ──
  JwtResult r;
  r.ok             = true;
  r.subject        = claims.value("sub", std::string{});
  r.email          = claims.value("email", std::string{});
  r.email_verified = claims.value("email_verified", false);
  r.name           = claims.value("name", std::string{});
  if (claims.contains("groups") && claims["groups"].is_array()) {
    for (const auto &g : claims["groups"])
      if (g.is_string()) r.groups.push_back(g.get<std::string>());
  }
  return r;
}

} // namespace sso
