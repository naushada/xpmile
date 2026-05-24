// modules/module/inhouseidp/test/jwks_test.cc
//
// Tests for jwks_from_keys + the JWKS HTTP endpoint adapter (Phase C).
// Mostly pure-function tests; the endpoint adapter is exercised against
// the same MockMongodbClient pattern used by other in-house IdP tests.

#include <cstdint>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "idp_jwks.hpp"
#include "json.hpp"
#include "mongodbc.hpp"
#include "sso_util.hpp"

namespace {

std::string read_file(const char *path) {
  std::ifstream f(path);
  if (!f) {
    ADD_FAILURE() << "could not read fixture: " << path;
    return {};
  }
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

class MockMongodbClient : public IMongodbClient {
public:
  std::string canned_get_documents;
  std::string database;

  const std::string &get_database() const override { return database; }
  std::string get_documents(const std::string &, const std::string &,
                             const std::string &) override {
    return canned_get_documents;
  }

  // Stubs.
  std::string create_document(const std::string &, const std::string &,
                              const std::string &) override { return {}; }
  std::int32_t create_bulk_document(const std::string &, const std::string &,
                                    const std::string &) override { return 0; }
  bool update_collection(const std::string &, const std::string &,
                         const std::string &) override { return false; }
  std::int32_t update_bulk_document(const std::string &,
                                    const std::vector<std::string> &,
                                    const std::vector<std::string> &) override { return 0; }
  bool delete_document(const std::string &, const std::string &) override { return false; }
  std::string get_document(const std::string &, const std::string &,
                            const std::string &) override { return {}; }
  std::string get_documents(const std::string &, const std::string &) override { return {}; }
  std::string next_awbno(const std::string & = "AWB") override { return {}; }
  std::string store_file(const std::string &, const std::string &,
                          const std::vector<std::uint8_t> &) override { return {}; }
  std::vector<std::uint8_t> fetch_file(const std::string &) override { return {}; }
  std::vector<std::uint8_t> fetch_file_by_id(const std::string &) override { return {}; }
  bool delete_file(const std::string &) override { return false; }
};

} // namespace

// ── jwks_from_keys ───────────────────────────────────────────────────────────

TEST(Jwks, EmptyKeysReturnsEmptyArray) {
  auto j = nlohmann::json::parse(idp::jwks_from_keys({}, /*now=*/123));
  ASSERT_TRUE(j.contains("keys"));
  ASSERT_TRUE(j["keys"].is_array());
  EXPECT_EQ(j["keys"].size(), 0u);
}

TEST(Jwks, BuildsJwkPerActiveKey) {
  const std::string pub = read_file(IDP_TEST_PUB_KEY_PATH);
  ASSERT_FALSE(pub.empty());

  std::vector<idp::SigningKeyView> keys = {
      {"kid-a", "RS256", pub, /*notAfter=*/0},
      {"kid-b", "RS256", pub, /*notAfter=*/0},
  };
  auto j = nlohmann::json::parse(idp::jwks_from_keys(keys, /*now=*/123));
  ASSERT_EQ(j["keys"].size(), 2u);
  EXPECT_EQ(j["keys"][0]["kty"], "RSA");
  EXPECT_EQ(j["keys"][0]["use"], "sig");
  EXPECT_EQ(j["keys"][0]["alg"], "RS256");
  EXPECT_EQ(j["keys"][0]["kid"], "kid-a");
  EXPECT_TRUE(j["keys"][0].contains("n"));
  EXPECT_TRUE(j["keys"][0].contains("e"));
  EXPECT_EQ(j["keys"][1]["kid"], "kid-b");
}

TEST(Jwks, ExcludesExpiredKeys) {
  const std::string pub = read_file(IDP_TEST_PUB_KEY_PATH);
  std::vector<idp::SigningKeyView> keys = {
      {"alive",   "RS256", pub, /*notAfter=*/2000},
      {"expired", "RS256", pub, /*notAfter=*/500},   // past
      {"forever", "RS256", pub, /*notAfter=*/0},     // no expiry
  };
  auto j = nlohmann::json::parse(idp::jwks_from_keys(keys, /*now=*/1000));
  ASSERT_EQ(j["keys"].size(), 2u);
  std::vector<std::string> kids;
  for (const auto &k : j["keys"]) kids.push_back(k["kid"]);
  EXPECT_NE(std::find(kids.begin(), kids.end(), "alive"),   kids.end());
  EXPECT_NE(std::find(kids.begin(), kids.end(), "forever"), kids.end());
  EXPECT_EQ(std::find(kids.begin(), kids.end(), "expired"), kids.end());
}

TEST(Jwks, ParsesRSAModulusCorrectly) {
  const std::string pub = read_file(IDP_TEST_PUB_KEY_PATH);
  std::vector<idp::SigningKeyView> keys = {{"k", "RS256", pub, 0}};
  auto j = nlohmann::json::parse(idp::jwks_from_keys(keys, 0));

  // For an RSA-2048 key, n is 256 bytes (or 257 with leading zero — but
  // OpenSSL's BN_bn2bin emits no leading zero). base64url, no padding,
  // → ceil(256 * 4/3) = 342 chars; but base64url drops padding so 342
  // chars minus 0/1/2 trailing '=' depending on length.
  const auto n_b64 = j["keys"][0]["n"].get<std::string>();
  const auto n_decoded = sso::base64url_decode(n_b64);
  EXPECT_EQ(n_decoded.size(), 256u)
      << "expected 2048-bit modulus = 256 bytes; got " << n_decoded.size();

  // e is typically 65537 = 0x010001 = 3 bytes.
  const auto e_b64 = j["keys"][0]["e"].get<std::string>();
  const auto e_decoded = sso::base64url_decode(e_b64);
  EXPECT_GE(e_decoded.size(), 1u);
  EXPECT_LE(e_decoded.size(), 8u);
}

TEST(Jwks, SkipsKeysWithNonRs256Alg) {
  const std::string pub = read_file(IDP_TEST_PUB_KEY_PATH);
  std::vector<idp::SigningKeyView> keys = {
      {"k1", "RS256", pub, 0},
      {"k2", "ES256", pub, 0},  // unsupported in v1
  };
  auto j = nlohmann::json::parse(idp::jwks_from_keys(keys, 0));
  ASSERT_EQ(j["keys"].size(), 1u);
  EXPECT_EQ(j["keys"][0]["kid"], "k1");
}

TEST(Jwks, SkipsKeysWithMalformedPem) {
  std::vector<idp::SigningKeyView> keys = {
      {"good",    "RS256", read_file(IDP_TEST_PUB_KEY_PATH), 0},
      {"garbage", "RS256", "not-a-pem", 0},
  };
  auto j = nlohmann::json::parse(idp::jwks_from_keys(keys, 0));
  ASSERT_EQ(j["keys"].size(), 1u);
  EXPECT_EQ(j["keys"][0]["kid"], "good");
}

// ── handle_idp_jwks_GET ──────────────────────────────────────────────────────

TEST(IdpJwksEndpoint, ReturnsJsonContentType_And200) {
  MockMongodbClient db;
  db.canned_get_documents = "[]";
  auto r = idp::handle_idp_jwks_GET(db, /*now=*/0);
  EXPECT_EQ(r.status, 200);
  EXPECT_EQ(r.content_type, "application/json");
  EXPECT_EQ(nlohmann::json::parse(r.body)["keys"].size(), 0u);
}

TEST(IdpJwksEndpoint, AssemblesKeysFromCollection) {
  // Production shape (matches Vaadin IdpSigningKeyService): `_id` is an
  // auto-generated ObjectId (omitted here — projection excludes it), the
  // domain kid lives in its OWN top-level `kid` field. The previous
  // seed used `{_id: "kid-1"}` which agreed with the OLD reader that
  // wrongly used `_id` as the kid; that hid the crash Vaadin keys hit
  // in production.
  const std::string pub = read_file(IDP_TEST_PUB_KEY_PATH);
  MockMongodbClient db;
  nlohmann::json arr = nlohmann::json::array();
  arr.push_back({
      {"kid",          "kid-1"},     // domain kid — what JWKS publishes
      {"alg",          "RS256"},
      {"publicKeyPem", pub}
  });
  db.canned_get_documents = arr.dump();

  auto r = idp::handle_idp_jwks_GET(db, /*now=*/0);
  EXPECT_EQ(r.status, 200);
  auto j = nlohmann::json::parse(r.body);
  ASSERT_EQ(j["keys"].size(), 1u);
  EXPECT_EQ(j["keys"][0]["kid"], "kid-1");
}

// Regression for the dyno-crash bug surfaced 2026-05-24 by the first
// real end-to-end walkthrough: when Vaadin's IdpSigningKeyService
// writes a signing key, `_id` is a Mongo ObjectId (canonical JSON
// serialises that as `{"$oid":"…"}` — an object, not a string).
// The old reader did `v.kid = row.value("_id", std::string{})`, and
// nlohmann::json::value() throws json::type_error.302 ("type must be
// string, but is object") on an object value — which aborted the
// dyno on the FIRST JWKS request after the first Vaadin-generated
// key landed.
TEST(IdpJwksEndpoint, DoesNotCrashOnObjectIdInId) {
  const std::string pub = read_file(IDP_TEST_PUB_KEY_PATH);
  MockMongodbClient db;
  nlohmann::json arr = nlohmann::json::array();
  arr.push_back({
      {"_id",          {{"$oid", "6a1297a9d1fd7b0d808e611d"}}},  // ObjectId, the offender
      {"kid",          "k-8736d197"},
      {"alg",          "RS256"},
      {"publicKeyPem", pub}
  });
  db.canned_get_documents = arr.dump();

  // Must NOT throw; the projection now drops `_id` so the reader
  // never even sees the object form — and even if it did, it reads
  // the separate `kid` string.
  auto r = idp::handle_idp_jwks_GET(db, /*now=*/0);
  EXPECT_EQ(r.status, 200);
  auto j = nlohmann::json::parse(r.body);
  ASSERT_EQ(j["keys"].size(), 1u);
  EXPECT_EQ(j["keys"][0]["kid"], "k-8736d197");
}
