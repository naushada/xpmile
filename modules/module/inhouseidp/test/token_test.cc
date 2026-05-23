// modules/module/inhouseidp/test/token_test.cc
//
// Tests for /token (Phase E). Covers:
//   - Happy path with a MockJwtSigner (assembles the JWT shape correctly,
//     stores the access_token, returns the standard JSON body).
//   - PKCE validation (verifier must match the stored S256 challenge).
//   - Replay defence (atomic claim — second use of the same code → 400).
//   - All the negative paths: expired, wrong client_id, wrong redirect_uri,
//     unsupported grant_type, signer failure, malformed inputs.
//   - End-to-end roundtrip: real RSA-2048 fixture key signs; verify the
//     resulting id_token with sso::verify_jwt against the JWKS built from
//     the matching public key.

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

#include "idp_token.hpp"
#include "json.hpp"
#include "jwt_signer.hpp"
#include "mongodbc.hpp"
#include "sso_jwt.hpp"
#include "sso_oidc.hpp"    // sso::code_challenge
#include "sso_session.hpp" // sso::IClock
#include "sso_util.hpp"    // base64url_encode

namespace {

class TestClock : public sso::IClock {
public:
  std::int64_t now = 1000;
  std::int64_t now_unix() const override { return now; }
};

/// Deterministic signer for the bulk of /token tests.
class MockJwtSigner : public idp::IJwtSigner {
public:
  std::vector<std::string> sign_calls;  // captured to_sign strings
  std::string fake_signature_raw = "FAKE_SIG_BYTES";
  std::string fake_kid           = "current";
  bool        return_error       = false;
  std::string error_msg          = "mock error";

  idp::SignJwtResult sign(const std::string &kid_hint,
                           const std::string &alg,
                           const std::string &to_sign) override {
    (void)kid_hint; (void)alg;
    sign_calls.push_back(to_sign);
    idp::SignJwtResult r;
    if (return_error) {
      r.ok    = false;
      r.error = error_msg;
      return r;
    }
    r.ok        = true;
    r.kid       = fake_kid;
    r.signature = sso::base64url_encode(
        reinterpret_cast<const unsigned char *>(fake_signature_raw.data()),
        fake_signature_raw.size());
    return r;
  }
};

class MockMongodbClient : public IMongodbClient {
public:
  struct CreateCall { std::string db, coll, doc; };
  struct UpdateCall { std::string coll, filter, update; };
  std::vector<CreateCall> creates;
  std::vector<UpdateCall> updates;

  std::map<std::string, std::string> canned_get_document;
  std::string database;

  bool update_returns = true;

  const std::string &get_database() const override { return database; }
  std::string create_document(const std::string &db, const std::string &coll,
                               const std::string &doc) override {
    creates.push_back({db, coll, doc});
    return "fake-oid";
  }
  std::string get_document(const std::string &coll, const std::string &,
                            const std::string &) override {
    auto it = canned_get_document.find(coll);
    return (it == canned_get_document.end()) ? std::string{} : it->second;
  }
  bool update_collection(const std::string &coll,
                          const std::string &filter,
                          const std::string &update) override {
    updates.push_back({coll, filter, update});
    return update_returns;
  }

  // ── stubs ───────────────────────────────────────────────────────────────
  std::int32_t create_bulk_document(const std::string &, const std::string &,
                                    const std::string &) override { return 0; }
  std::int32_t update_bulk_document(const std::string &,
                                    const std::vector<std::string> &,
                                    const std::vector<std::string> &) override { return 0; }
  bool delete_document(const std::string &, const std::string &) override { return false; }
  std::string get_documents(const std::string &, const std::string &,
                             const std::string &) override { return {}; }
  std::string get_documents(const std::string &, const std::string &) override { return {}; }
  std::string next_awbno(const std::string & = "AWB") override { return {}; }
  std::string store_file(const std::string &, const std::string &,
                          const std::vector<std::uint8_t> &) override { return {}; }
  std::vector<std::uint8_t> fetch_file(const std::string &) override { return {}; }
  std::vector<std::uint8_t> fetch_file_by_id(const std::string &) override { return {}; }
  bool delete_file(const std::string &) override { return false; }
};

const std::string kCodeId       = "C1";
const std::string kVerifier     = "test-verifier-123456789012345678901234567";
const std::string kClientId     = "xpmile-spa";
const std::string kRedirectUri  = "https://marvel.app/cb";
const std::string kIssuer       = "https://idp.example/api/v1/idp";

nlohmann::json seed_code(MockMongodbClient &db, std::int64_t expires_at = 99999,
                          const std::string &challenge = {}) {
  // If caller didn't override, compute the matching challenge from kVerifier.
  std::string ch = challenge.empty() ? sso::code_challenge(kVerifier) : challenge;
  nlohmann::json doc = {
      {"_id",                   kCodeId},
      {"client_id",             kClientId},
      {"user_sub",              "alice"},
      {"user_email",            "alice@x.com"},
      {"user_name",             "Alice"},
      {"user_role",             "Customer"},
      {"redirect_uri",          kRedirectUri},
      {"nonce",                 "RP-NONCE"},
      {"scope",                 "openid email"},
      {"code_challenge",        ch},
      {"code_challenge_method", "S256"},
      {"expiresAt",             expires_at},
  };
  db.canned_get_document["idp_codes"] = doc.dump();
  return doc;
}

std::map<std::string, std::string> valid_form() {
  return {
      {"grant_type",    "authorization_code"},
      {"code",          kCodeId},
      {"code_verifier", kVerifier},
      {"client_id",     kClientId},
      {"redirect_uri",  kRedirectUri},
  };
}

std::string read_file(const char *p) {
  std::ifstream f(p); std::stringstream ss; ss << f.rdbuf(); return ss.str();
}

} // namespace

// ── happy path with mock signer ──────────────────────────────────────────────

TEST(IdpToken, ValidCode_AndVerifier_ReturnsTokenPair_AndStandardBody) {
  MockMongodbClient db;
  MockJwtSigner signer;
  TestClock clock; clock.now = 1000;
  seed_code(db);

  auto r = idp::token(valid_form(), db, signer, kIssuer, clock);

  ASSERT_EQ(r.status, 200);
  auto j = nlohmann::json::parse(r.body);
  EXPECT_EQ(j["token_type"], "Bearer");
  EXPECT_EQ(j["expires_in"], 3600);
  EXPECT_EQ(j["scope"],      "openid email");
  EXPECT_TRUE(j.contains("access_token"));
  EXPECT_TRUE(j.contains("id_token"));

  // id_token shape: header.payload.signature with two dots
  const std::string id_token = j["id_token"].get<std::string>();
  std::size_t dot1 = id_token.find('.');
  std::size_t dot2 = id_token.find('.', dot1 + 1);
  ASSERT_NE(dot1, std::string::npos);
  ASSERT_NE(dot2, std::string::npos);

  // The atomic claim went through.
  ASSERT_EQ(db.updates.size(), 1u);
  EXPECT_EQ(db.updates[0].coll, "idp_codes");
  EXPECT_NE(db.updates[0].filter.find("\"consumed\""), std::string::npos);

  // The access_token row got persisted.
  ASSERT_EQ(db.creates.size(), 1u);
  EXPECT_EQ(db.creates[0].coll, "idp_access_tokens");
}

TEST(IdpToken, IdTokenPayload_CarriesAllExpectedClaims) {
  MockMongodbClient db;
  MockJwtSigner signer;
  signer.fake_kid = "current";  // single-sign path (no re-sign)
  TestClock clock; clock.now = 1000;
  seed_code(db);

  auto r = idp::token(valid_form(), db, signer, kIssuer, clock);
  ASSERT_EQ(r.status, 200);

  // Parse the id_token payload (middle segment).
  auto id_token = nlohmann::json::parse(r.body)["id_token"].get<std::string>();
  const std::size_t dot1 = id_token.find('.');
  const std::size_t dot2 = id_token.find('.', dot1 + 1);
  const std::string payload_b64u = id_token.substr(dot1 + 1, dot2 - dot1 - 1);
  const std::string payload_json = sso::base64url_decode(payload_b64u);
  auto payload = nlohmann::json::parse(payload_json);

  EXPECT_EQ(payload["iss"],   kIssuer);
  EXPECT_EQ(payload["sub"],   "alice");
  EXPECT_EQ(payload["aud"],   kClientId);
  EXPECT_EQ(payload["exp"].get<std::int64_t>(), 1000 + 3600);
  EXPECT_EQ(payload["iat"].get<std::int64_t>(), 1000);
  EXPECT_EQ(payload["nonce"], "RP-NONCE");
  EXPECT_EQ(payload["email"], "alice@x.com");
  EXPECT_EQ(payload["name"],  "Alice");
  EXPECT_EQ(payload["role"],  "Customer");
}

// ── negative paths ──────────────────────────────────────────────────────────

TEST(IdpToken, UnsupportedGrantType_Returns_400) {
  MockMongodbClient db; MockJwtSigner signer; TestClock clock;
  seed_code(db);
  auto f = valid_form();
  f["grant_type"] = "refresh_token";
  auto r = idp::token(f, db, signer, kIssuer, clock);
  EXPECT_EQ(r.status, 400);
  EXPECT_NE(r.body.find("unsupported_grant_type"), std::string::npos);
}

TEST(IdpToken, MissingCode_Returns_400) {
  MockMongodbClient db; MockJwtSigner signer; TestClock clock;
  auto f = valid_form();
  f.erase("code");
  auto r = idp::token(f, db, signer, kIssuer, clock);
  EXPECT_EQ(r.status, 400);
}

TEST(IdpToken, MissingVerifier_Returns_400) {
  MockMongodbClient db; MockJwtSigner signer; TestClock clock;
  auto f = valid_form();
  f.erase("code_verifier");
  auto r = idp::token(f, db, signer, kIssuer, clock);
  EXPECT_EQ(r.status, 400);
}

TEST(IdpToken, UnknownCode_Returns_400_InvalidGrant) {
  MockMongodbClient db; MockJwtSigner signer; TestClock clock;
  // No seed → get_document returns empty.
  auto r = idp::token(valid_form(), db, signer, kIssuer, clock);
  EXPECT_EQ(r.status, 400);
  EXPECT_NE(r.body.find("invalid_grant"), std::string::npos);
}

TEST(IdpToken, ExpiredCode_Returns_400) {
  MockMongodbClient db; MockJwtSigner signer; TestClock clock;
  clock.now = 100000;
  seed_code(db, /*expires_at=*/500);  // long past
  auto r = idp::token(valid_form(), db, signer, kIssuer, clock);
  EXPECT_EQ(r.status, 400);
}

TEST(IdpToken, WrongClientId_Returns_400) {
  MockMongodbClient db; MockJwtSigner signer; TestClock clock;
  seed_code(db);
  auto f = valid_form();
  f["client_id"] = "different-rp";
  auto r = idp::token(f, db, signer, kIssuer, clock);
  EXPECT_EQ(r.status, 400);
}

TEST(IdpToken, MismatchedRedirectUri_Returns_400) {
  MockMongodbClient db; MockJwtSigner signer; TestClock clock;
  seed_code(db);
  auto f = valid_form();
  f["redirect_uri"] = "https://evil.com/cb";
  auto r = idp::token(f, db, signer, kIssuer, clock);
  EXPECT_EQ(r.status, 400);
}

TEST(IdpToken, WrongPKCEVerifier_Returns_400) {
  MockMongodbClient db; MockJwtSigner signer; TestClock clock;
  seed_code(db);
  auto f = valid_form();
  f["code_verifier"] = "totally-different-verifier-xxxxxxxxxxxxxxxxxxxx";
  auto r = idp::token(f, db, signer, kIssuer, clock);
  EXPECT_EQ(r.status, 400);
  EXPECT_NE(r.body.find("PKCE"), std::string::npos);
}

TEST(IdpToken, ReplayedCode_Returns_400) {
  MockMongodbClient db; MockJwtSigner signer; TestClock clock;
  seed_code(db);
  db.update_returns = false;  // simulate "someone already consumed"
  auto r = idp::token(valid_form(), db, signer, kIssuer, clock);
  EXPECT_EQ(r.status, 400);
  EXPECT_NE(r.body.find("already used"), std::string::npos);
}

TEST(IdpToken, SignerError_Returns_500) {
  MockMongodbClient db; MockJwtSigner signer; TestClock clock;
  signer.return_error = true;
  signer.error_msg    = "wsdbagent timeout";
  seed_code(db);
  auto r = idp::token(valid_form(), db, signer, kIssuer, clock);
  EXPECT_EQ(r.status, 500);
  EXPECT_NE(r.body.find("wsdbagent timeout"), std::string::npos);
}

// ── end-to-end with real RSA ─────────────────────────────────────────────────

namespace {

class FixtureRsaSigner : public idp::IJwtSigner {
public:
  std::string kid_used = "fixture-kid";
  idp::SignJwtResult sign(const std::string &,
                           const std::string &,
                           const std::string &to_sign) override {
    idp::SignJwtResult r;
    const std::string priv_pem = read_file(IDP_TEST_PRIV_KEY_PATH);
    if (priv_pem.empty()) { r.error = "no fixture"; return r; }

    std::unique_ptr<BIO, decltype(&BIO_free_all)> bio{
        BIO_new_mem_buf(priv_pem.data(), static_cast<int>(priv_pem.size())),
        BIO_free_all};
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> pkey{
        PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr),
        EVP_PKEY_free};
    if (!pkey) { r.error = "bad PEM"; return r; }

    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx{
        EVP_MD_CTX_new(), EVP_MD_CTX_free};
    if (1 != EVP_DigestSignInit(ctx.get(), nullptr, EVP_sha256(),
                                 nullptr, pkey.get())) {
      r.error = "DigestSignInit"; return r;
    }
    if (1 != EVP_DigestSignUpdate(ctx.get(),
                                   to_sign.data(), to_sign.size())) {
      r.error = "DigestSignUpdate"; return r;
    }
    std::size_t slen = 0;
    EVP_DigestSignFinal(ctx.get(), nullptr, &slen);
    std::vector<std::uint8_t> sig(slen);
    EVP_DigestSignFinal(ctx.get(), sig.data(), &slen);
    sig.resize(slen);

    r.ok        = true;
    r.kid       = kid_used;
    r.signature = sso::base64url_encode(sig.data(), sig.size());
    return r;
  }
};

// Build a JWKS document containing the fixture public key under the given kid.
std::string fixture_jwks(const std::string &kid) {
  const std::string pub = read_file(IDP_TEST_PUB_KEY_PATH);
  std::unique_ptr<BIO, decltype(&BIO_free_all)> bio{
      BIO_new_mem_buf(pub.data(), static_cast<int>(pub.size())),
      BIO_free_all};
  std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> pkey{
      PEM_read_bio_PUBKEY(bio.get(), nullptr, nullptr, nullptr),
      EVP_PKEY_free};
  RSA *rsa = EVP_PKEY_get0_RSA(pkey.get());
  const BIGNUM *bn_n = nullptr, *bn_e = nullptr;
  RSA_get0_key(rsa, &bn_n, &bn_e, nullptr);
  std::vector<std::uint8_t> n(BN_num_bytes(bn_n)), e(BN_num_bytes(bn_e));
  BN_bn2bin(bn_n, n.data());
  BN_bn2bin(bn_e, e.data());
  nlohmann::json jwks = {{"keys", nlohmann::json::array({
      {{"kty", "RSA"}, {"use", "sig"}, {"alg", "RS256"}, {"kid", kid},
       {"n", sso::base64url_encode(n.data(), n.size())},
       {"e", sso::base64url_encode(e.data(), e.size())}},
  })}};
  return jwks.dump();
}

} // namespace

TEST(IdpToken, EndToEnd_RealRsaSign_VerifyWithJwks) {
  MockMongodbClient db;
  FixtureRsaSigner signer;
  TestClock clock; clock.now = 1000;
  seed_code(db);

  auto r = idp::token(valid_form(), db, signer, kIssuer, clock);
  ASSERT_EQ(r.status, 200) << r.body;

  const auto id_token = nlohmann::json::parse(r.body)["id_token"].get<std::string>();

  // Verify with the v1.0 sso::verify_jwt primitive against a JWKS that
  // contains our fixture public key under kid="fixture-kid".
  sso::Jwks jwks;
  ASSERT_TRUE(jwks.parse(fixture_jwks(signer.kid_used)));
  sso::JwtExpect expect;
  expect.issuer    = kIssuer;
  expect.client_id = kClientId;
  expect.nonce     = "RP-NONCE";
  // verify_jwt enforces exp/iat with a small clock skew; pass clock.now so
  // the timestamps match.
  auto result = sso::verify_jwt(id_token, jwks, expect, clock.now);
  EXPECT_TRUE(result.ok) << "verify_jwt failed: " << result.error;
  EXPECT_EQ(result.subject, "alice");
  EXPECT_EQ(result.email,   "alice@x.com");
}
