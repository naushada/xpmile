#include <cctype>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/valid.h>

#include <xmlsec/xmlsec.h>
#include <xmlsec/xmldsig.h>
#include <xmlsec/xmltree.h>
#include <xmlsec/crypto.h>

#include "json.hpp"
#include "mongodbc.hpp"
#include "saml_provider.hpp"
#include "saml_response.hpp"
#include "saml_signature.hpp"
#include "sso_authz.hpp"
#include "sso_config.hpp"
#include "sso_csrf.hpp"
#include "sso_cookie.hpp"
#include "sso_endpoints.hpp"
#include "sso_http_client.hpp"
#include "sso_jwt.hpp"
#include "sso_oidc.hpp"
#include "sso_provisioning.hpp"
#include "sso_registry.hpp"
#include "sso_session.hpp"
#include "sso_util.hpp"

// ═══════════════════════════════════════════════════════════════════════════════
// Test doubles
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

// Deterministic, settable time source.
class FakeClock : public sso::IClock {
public:
  std::int64_t t = 0;
  std::int64_t now_unix() const override { return t; }
};

// Canned-response + spy test double for IMongodbClient. Only the operations
// the session store exercises carry state; the rest are inert stubs.
class MockMongodbClient : public IMongodbClient {
public:
  // canned response for get_document()
  std::string getDocumentResult;

  // spy fields
  std::string lastCreateColl, lastCreateDoc;
  std::string lastGetColl, lastGetQuery;
  std::string lastUpdateColl, lastUpdateFilter, lastUpdateDoc;
  std::string lastDeleteColl, lastDeleteDoc;
  int updateCount = 0;

  const std::string &get_database() const override { return m_db; }

  std::string create_document(const std::string &, const std::string &coll,
                              const std::string &doc) override {
    lastCreateColl = coll;
    lastCreateDoc  = doc;
    return "oid000000000000000000000001";
  }

  std::int32_t create_bulk_document(const std::string &, const std::string &,
                                    const std::string &) override {
    return 0;
  }

  bool txnClaimed = false;

  bool update_collection(const std::string &coll, const std::string &filter,
                         const std::string &document) override {
    lastUpdateColl   = coll;
    lastUpdateFilter = filter;
    lastUpdateDoc    = document;
    ++updateCount;
    // Model the atomic single-use claim of an sso_transactions document:
    // the guarded update matches once, then never again.
    if (coll == "sso_transactions") {
      const bool first = !txnClaimed;
      txnClaimed = true;
      return first;
    }
    return true;
  }

  std::int32_t update_bulk_document(
      const std::string &, const std::vector<std::string> &,
      const std::vector<std::string> &) override {
    return 0;
  }

  bool delete_document(const std::string &coll,
                       const std::string &doc) override {
    lastDeleteColl = coll;
    lastDeleteDoc  = doc;
    return true;
  }

  // Account lookups distinguish the subject-match query (carries
  // "ssoIdentities") from the email-match query.
  std::string accountBySubject;
  std::string accountByEmail;

  std::string get_document(const std::string &coll, const std::string &query,
                           const std::string &) override {
    lastGetColl  = coll;
    lastGetQuery = query;
    if (coll == "account") {
      return query.find("ssoIdentities") != std::string::npos
                 ? accountBySubject
                 : accountByEmail;
    }
    return getDocumentResult;
  }

  std::string get_documents(const std::string &, const std::string &,
                            const std::string &) override {
    return {};
  }

  std::string get_documents(const std::string &,
                            const std::string &) override {
    return {};
  }

  std::string next_awbno(const std::string &) override { return {}; }

  std::string store_file(const std::string &, const std::string &,
                         const std::vector<std::uint8_t> &) override {
    return {};
  }

  std::vector<std::uint8_t> fetch_file(const std::string &) override {
    return {};
  }

  std::vector<std::uint8_t> fetch_file_by_id(const std::string &) override {
    return {};
  }

  bool delete_file(const std::string &) override { return false; }

private:
  std::string m_db = "testdb";
};

// Canned-response + spy test double for IHttpClient.
class MockHttpClient : public sso::IHttpClient {
public:
  std::map<std::string, sso::HttpResponse> responses;  // keyed by URL
  std::string lastGetUrl;
  std::string lastPostUrl;
  std::string lastPostBody;  // the encoded form body

  sso::HttpResponse get(const std::string &url) override {
    lastGetUrl = url;
    auto it = responses.find(url);
    return it != responses.end() ? it->second : sso::HttpResponse{};
  }

  sso::HttpResponse post_form(
      const std::string &url,
      const std::map<std::string, std::string> &fields) override {
    lastPostUrl  = url;
    lastPostBody = sso::encode_form(fields);
    auto it = responses.find(url);
    return it != responses.end() ? it->second : sso::HttpResponse{};
  }
};

// Canned test double for IIdentityProvider — returns scripted results so the
// SSO endpoint logic can be tested without a real OIDC/SAML round trip.
class MockIdentityProvider : public sso::IIdentityProvider {
public:
  std::string         m_id = "mock";
  sso::AuthnRequest   beginResult;
  sso::IdentityClaims callbackResult;

  const std::string &id() const override { return m_id; }

  sso::AuthnRequest begin_login(const std::string &) override {
    return beginResult;
  }

  sso::IdentityClaims handle_callback(const std::string &,
                                      const std::string &) override {
    return callbackResult;
  }
};

// ── JWT test helpers — real RSA keys + real signatures ──────────────────────

// Generate a 2048-bit RSA keypair.
sso::EvpPkeyPtr generate_rsa() {
  EVP_PKEY     *pkey = nullptr;
  EVP_PKEY_CTX *ctx  = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
  if (ctx && EVP_PKEY_keygen_init(ctx) == 1 &&
      EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048) == 1) {
    EVP_PKEY_keygen(ctx, &pkey);
  }
  if (ctx) EVP_PKEY_CTX_free(ctx);
  return sso::EvpPkeyPtr(pkey);
}

// A single JWKS entry (jwk object) for the public part of `key`.
nlohmann::json jwk_of(EVP_PKEY *key, const std::string &kid) {
  RSA *rsa = EVP_PKEY_get0_RSA(key);  // non-owning
  const BIGNUM *n = nullptr;
  const BIGNUM *e = nullptr;
  RSA_get0_key(rsa, &n, &e, nullptr);
  std::vector<unsigned char> nb(static_cast<std::size_t>(BN_num_bytes(n)));
  std::vector<unsigned char> eb(static_cast<std::size_t>(BN_num_bytes(e)));
  BN_bn2bin(n, nb.data());
  BN_bn2bin(e, eb.data());
  return nlohmann::json{{"kty", "RSA"},
                        {"kid", kid},
                        {"alg", "RS256"},
                        {"n", sso::base64url_encode(nb.data(), nb.size())},
                        {"e", sso::base64url_encode(eb.data(), eb.size())}};
}

// A complete JWKS document containing one key.
std::string make_jwks(EVP_PKEY *key, const std::string &kid) {
  return nlohmann::json{{"keys", nlohmann::json::array({jwk_of(key, kid)})}}
      .dump();
}

// Sign a JWT (compact serialization) with RS256.
std::string sign_jwt(EVP_PKEY *key, const nlohmann::json &header,
                     const nlohmann::json &payload) {
  auto b64 = [](const std::string &s) {
    return sso::base64url_encode(
        reinterpret_cast<const unsigned char *>(s.data()), s.size());
  };
  const std::string signing_input =
      b64(header.dump()) + "." + b64(payload.dump());

  std::string  sig;
  EVP_MD_CTX  *ctx = EVP_MD_CTX_new();
  if (ctx &&
      EVP_DigestSignInit(ctx, nullptr, EVP_sha256(), nullptr, key) == 1 &&
      EVP_DigestSignUpdate(ctx, signing_input.data(),
                           signing_input.size()) == 1) {
    std::size_t len = 0;
    if (EVP_DigestSignFinal(ctx, nullptr, &len) == 1) {
      std::vector<unsigned char> buf(len);
      if (EVP_DigestSignFinal(ctx, buf.data(), &len) == 1)
        sig = sso::base64url_encode(buf.data(), len);
    }
  }
  if (ctx) EVP_MD_CTX_free(ctx);
  return signing_input + "." + sig;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════════
// Phase A.1 — session cookie helpers
// ═══════════════════════════════════════════════════════════════════════════════

TEST(SsoCookieTest, Build_SetsAllSecurityAttributes) {
  const std::string c = sso::build_session_cookie("abc");
  EXPECT_NE(c.find("HttpOnly"), std::string::npos);
  EXPECT_NE(c.find("Secure"), std::string::npos);
  EXPECT_NE(c.find("SameSite=Lax"), std::string::npos);
  EXPECT_NE(c.find("Path=/"), std::string::npos);
  EXPECT_NE(c.find("Max-Age="), std::string::npos);
}

TEST(SsoCookieTest, Build_EmbedsSidValue) {
  EXPECT_NE(sso::build_session_cookie("abc").find("xpmile_session=abc"),
            std::string::npos);
}

TEST(SsoCookieTest, BuildExpired_HasMaxAgeZero) {
  EXPECT_NE(sso::build_expired_cookie().find("Max-Age=0"), std::string::npos);
}

TEST(SsoCookieTest, Parse_ExtractsSidFromCookieHeader) {
  EXPECT_EQ(sso::parse_session_cookie("foo=1; xpmile_session=abc; bar=2"),
            "abc");
}

TEST(SsoCookieTest, Parse_MissingCookie_ReturnsEmpty) {
  EXPECT_EQ(sso::parse_session_cookie("foo=1; bar=2"), "");
}

TEST(SsoCookieTest, Parse_EmptyHeader_ReturnsEmpty) {
  EXPECT_EQ(sso::parse_session_cookie(""), "");
}

TEST(SsoCookieTest, Parse_MalformedHeader_DoesNotCrash) {
  EXPECT_EQ(sso::parse_session_cookie("garbage;;==;"), "");
}

// ═══════════════════════════════════════════════════════════════════════════════
// Phase A.2 — SSO_CONFIG parser
// ═══════════════════════════════════════════════════════════════════════════════

TEST(SsoConfigTest, Parse_ValidOidcProvider_PopulatesFields) {
  const char *json = R"({
    "publicBaseUrl": "https://marvel-3a78bd953f5f.herokuapp.com",
    "providers": [{
      "id": "corp",
      "displayName": "Acme Corp SSO",
      "protocol": "oidc",
      "issuer": "https://acme.okta.com/oauth2/default",
      "clientId": "xpmile-prod",
      "clientSecret": "shh",
      "scopes": ["openid", "email", "profile", "groups"],
      "defaultRole": "Customer",
      "allowedEmailDomains": ["acme.com"]
    }]
  })";
  sso::SsoConfig cfg;
  std::string err;
  ASSERT_TRUE(sso::parse_sso_config(json, cfg, err)) << err;
  ASSERT_EQ(cfg.providers.size(), 1u);

  const sso::ProviderConfig &p = cfg.providers[0];
  EXPECT_EQ(p.protocol, sso::Protocol::Oidc);
  EXPECT_EQ(p.issuer, "https://acme.okta.com/oauth2/default");
  EXPECT_EQ(p.client_id, "xpmile-prod");
  EXPECT_EQ(p.client_secret, "shh");
  ASSERT_EQ(p.scopes.size(), 4u);
  EXPECT_EQ(p.scopes[0], "openid");
  EXPECT_EQ(p.default_role, "Customer");
  ASSERT_EQ(p.allowed_email_domains.size(), 1u);
  EXPECT_EQ(p.allowed_email_domains[0], "acme.com");
}

TEST(SsoConfigTest, Parse_ValidSamlProvider_PopulatesFields) {
  const char *json = R"({
    "publicBaseUrl": "https://marvel-3a78bd953f5f.herokuapp.com",
    "providers": [{
      "id": "partner",
      "displayName": "Partner SAML",
      "protocol": "saml",
      "idpEntityId": "https://idp.partner.com/saml",
      "idpSsoUrl": "https://idp.partner.com/saml/sso",
      "idpSigningCert": "-----BEGIN CERTIFICATE-----\nMIIB\n-----END CERTIFICATE-----",
      "spEntityId": "xpmile-marvel"
    }]
  })";
  sso::SsoConfig cfg;
  std::string err;
  ASSERT_TRUE(sso::parse_sso_config(json, cfg, err)) << err;
  ASSERT_EQ(cfg.providers.size(), 1u);

  const sso::ProviderConfig &p = cfg.providers[0];
  EXPECT_EQ(p.protocol, sso::Protocol::Saml);
  EXPECT_EQ(p.idp_entity_id, "https://idp.partner.com/saml");
  EXPECT_EQ(p.idp_sso_url, "https://idp.partner.com/saml/sso");
  EXPECT_FALSE(p.idp_signing_cert.empty());
  EXPECT_EQ(p.sp_entity_id, "xpmile-marvel");
}

TEST(SsoConfigTest, Parse_MultipleProviders_AllLoaded) {
  const char *json = R"({
    "publicBaseUrl": "https://marvel-3a78bd953f5f.herokuapp.com",
    "providers": [
      {"id": "a", "protocol": "oidc"},
      {"id": "b", "protocol": "saml"}
    ]
  })";
  sso::SsoConfig cfg;
  std::string err;
  ASSERT_TRUE(sso::parse_sso_config(json, cfg, err)) << err;
  ASSERT_EQ(cfg.providers.size(), 2u);
  EXPECT_EQ(cfg.providers[0].id, "a");
  EXPECT_EQ(cfg.providers[1].id, "b");
}

TEST(SsoConfigTest, Parse_EmptyConfig_NoProviders_NoCrash) {
  const char *json =
      R"({"publicBaseUrl": "https://marvel-3a78bd953f5f.herokuapp.com", "providers": []})";
  sso::SsoConfig cfg;
  std::string err;
  ASSERT_TRUE(sso::parse_sso_config(json, cfg, err)) << err;
  EXPECT_TRUE(cfg.providers.empty());
}

TEST(SsoConfigTest, Parse_MalformedJson_ReturnsError) {
  sso::SsoConfig cfg;
  std::string err;
  EXPECT_FALSE(sso::parse_sso_config("{not json", cfg, err));
  EXPECT_FALSE(err.empty());
}

TEST(SsoConfigTest, Parse_MissingPublicBaseUrl_ReturnsError) {
  sso::SsoConfig cfg;
  std::string err;
  EXPECT_FALSE(sso::parse_sso_config(R"({"providers": []})", cfg, err));
  EXPECT_FALSE(err.empty());
}

TEST(SsoConfigTest, Parse_GroupRoleMap_DefaultsToDisabled_WhenAbsent) {
  const char *json = R"({
    "publicBaseUrl": "https://marvel-3a78bd953f5f.herokuapp.com",
    "providers": [{"id": "corp", "protocol": "oidc"}]
  })";
  sso::SsoConfig cfg;
  std::string err;
  ASSERT_TRUE(sso::parse_sso_config(json, cfg, err)) << err;
  ASSERT_EQ(cfg.providers.size(), 1u);
  EXPECT_FALSE(cfg.providers[0].group_role_map_enabled);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Phase A.3 — SessionManager
// ═══════════════════════════════════════════════════════════════════════════════

TEST(SessionManagerTest, Create_WritesSessionDoc_ReturnsSid) {
  MockMongodbClient db;
  FakeClock clock;
  sso::SessionManager sm(db, clock);

  sso::NewSessionParams p;
  p.account_code = "acme-ops";
  p.role         = "Admin";
  p.auth_method  = sso::AuthMethod::Password;

  const std::string sid = sm.create_session(p);
  EXPECT_EQ(db.lastCreateColl, "sessions");
  EXPECT_FALSE(sid.empty());
  EXPECT_GE(sid.size(), 32u);  // ≥ 32 bytes of entropy, base64url-encoded
}

TEST(SessionManagerTest, Create_SidsAreUnique) {
  MockMongodbClient db;
  FakeClock clock;
  sso::SessionManager sm(db, clock);

  sso::NewSessionParams p;
  p.account_code = "acme-ops";
  EXPECT_NE(sm.create_session(p), sm.create_session(p));
}

TEST(SessionManagerTest, Create_SetsExpiresAt_FromClock) {
  MockMongodbClient db;
  FakeClock clock;
  clock.t = 1000000;
  sso::SessionManager sm(db, clock, /*max_age=*/3600);

  sso::NewSessionParams p;
  p.account_code = "acme-ops";
  sm.create_session(p);

  const nlohmann::json j = nlohmann::json::parse(db.lastCreateDoc);
  EXPECT_EQ(j["expiresAt"].get<std::int64_t>(), 1000000 + 3600);
}

TEST(SessionManagerTest, Lookup_ValidSid_ReturnsAuthContext) {
  MockMongodbClient db;
  FakeClock clock;
  clock.t = 1000;
  db.getDocumentResult =
      R"({"_id":"s1","accountCode":"acme-ops","role":"Admin",)"
      R"("authMethod":"oidc","createdAt":0,"lastSeenAt":0,"expiresAt":999999999})";
  sso::SessionManager sm(db, clock);

  const sso::AuthContext ctx = sm.lookup("s1");
  EXPECT_TRUE(ctx.valid);
  EXPECT_EQ(ctx.account_code, "acme-ops");
  EXPECT_EQ(ctx.role, "Admin");
  EXPECT_EQ(ctx.auth_method, sso::AuthMethod::Oidc);
}

TEST(SessionManagerTest, Lookup_ExpiredSession_ReturnsInvalid) {
  MockMongodbClient db;
  FakeClock clock;
  clock.t = 1000;
  db.getDocumentResult =
      R"({"_id":"s1","accountCode":"acme-ops","role":"Admin",)"
      R"("authMethod":"password","expiresAt":500})";
  sso::SessionManager sm(db, clock);
  EXPECT_FALSE(sm.lookup("s1").valid);
}

TEST(SessionManagerTest, Lookup_UnknownSid_ReturnsInvalid) {
  MockMongodbClient db;
  FakeClock clock;
  db.getDocumentResult = "";  // no such session
  sso::SessionManager sm(db, clock);
  EXPECT_FALSE(sm.lookup("nope").valid);
}

TEST(SessionManagerTest, Revoke_DeletesSessionDoc) {
  MockMongodbClient db;
  FakeClock clock;
  sso::SessionManager sm(db, clock);

  sm.revoke("s1");
  EXPECT_EQ(db.lastDeleteColl, "sessions");
  EXPECT_NE(db.lastDeleteDoc.find("s1"), std::string::npos);
}

TEST(SessionManagerTest, Lookup_RefreshesLastSeen_AtMostOncePerMinute) {
  MockMongodbClient db;
  FakeClock clock;
  clock.t = 1000;
  db.getDocumentResult =
      R"({"_id":"s1","accountCode":"a","role":"r",)"
      R"("authMethod":"password","expiresAt":999999999})";
  sso::SessionManager sm(db, clock);

  sm.lookup("s1");
  clock.t = 1010;  // 10s later — within the 60s throttle window
  sm.lookup("s1");
  EXPECT_EQ(db.updateCount, 1);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Phase A.4 — SessionCache + SessionManager cache integration
// ═══════════════════════════════════════════════════════════════════════════════

TEST(SessionCacheTest, Put_ThenGet_ReturnsEntry) {
  FakeClock clock;
  sso::SessionCache cache(clock);

  sso::AuthContext ctx;
  ctx.valid        = true;
  ctx.account_code = "acme-ops";
  ctx.role         = "Admin";
  cache.put("s1", ctx);

  const std::optional<sso::AuthContext> got = cache.get("s1");
  ASSERT_TRUE(got.has_value());
  EXPECT_EQ(got->account_code, "acme-ops");
  EXPECT_EQ(got->role, "Admin");
}

TEST(SessionCacheTest, Get_AfterTtlExpiry_Misses) {
  FakeClock clock;
  clock.t = 1000;
  sso::SessionCache cache(clock, /*ttl_secs=*/60);

  sso::AuthContext ctx;
  ctx.valid = true;
  cache.put("s1", ctx);

  clock.t = 1000 + 61;  // past the TTL
  EXPECT_FALSE(cache.get("s1").has_value());
}

TEST(SessionCacheTest, Erase_RemovesEntry) {
  FakeClock clock;
  sso::SessionCache cache(clock);

  sso::AuthContext ctx;
  ctx.valid = true;
  cache.put("s1", ctx);
  cache.erase("s1");
  EXPECT_FALSE(cache.get("s1").has_value());
}

TEST(SessionCacheTest, Eviction_RespectsMaxSize) {
  FakeClock clock;
  sso::SessionCache cache(clock, /*ttl_secs=*/60, /*capacity=*/2);

  sso::AuthContext ctx;
  ctx.valid = true;
  cache.put("s1", ctx);
  cache.put("s2", ctx);
  cache.put("s3", ctx);  // overflows — s1 (least-recently used) is evicted

  EXPECT_FALSE(cache.get("s1").has_value());
  EXPECT_TRUE(cache.get("s2").has_value());
  EXPECT_TRUE(cache.get("s3").has_value());
  EXPECT_LE(cache.size(), 2u);
}

TEST(SessionCacheTest, ConcurrentAccess_NoDataRace) {
  FakeClock clock;
  sso::SessionCache cache(clock, /*ttl_secs=*/60, /*capacity=*/256);

  std::vector<std::thread> threads;
  for (int t = 0; t < 8; ++t) {
    threads.emplace_back([&cache, t]() {
      for (int i = 0; i < 500; ++i) {
        sso::AuthContext c;
        c.valid        = true;
        c.account_code = "acct";
        const std::string sid = "s" + std::to_string((t * 500 + i) % 100);
        cache.put(sid, c);
        cache.get(sid);
        if (i % 7 == 0) cache.erase(sid);
      }
    });
  }
  for (std::thread &th : threads) th.join();
  SUCCEED();  // mutex-guarded — no crash, no data race
}

TEST(SessionManagerTest, Lookup_SecondCall_HitsCache_NoDbCall) {
  MockMongodbClient db;
  FakeClock clock;
  clock.t = 1000;
  db.getDocumentResult =
      R"({"_id":"s1","accountCode":"acme-ops","role":"Admin",)"
      R"("authMethod":"oidc","expiresAt":999999999})";
  sso::SessionManager sm(db, clock);

  ASSERT_TRUE(sm.lookup("s1").valid);  // cache miss — populates the cache
  db.lastGetColl.clear();              // reset the spy
  ASSERT_TRUE(sm.lookup("s1").valid);  // cache hit
  EXPECT_TRUE(db.lastGetColl.empty())
      << "second lookup must be served from the cache, not the DB";
}

TEST(SessionManagerTest, Lookup_AfterCacheTtlExpiry_FallsThroughToDb) {
  MockMongodbClient db;
  FakeClock clock;
  clock.t = 1000;
  db.getDocumentResult =
      R"({"_id":"s1","accountCode":"a","role":"r",)"
      R"("authMethod":"password","expiresAt":999999999})";
  sso::SessionManager sm(db, clock);

  sm.lookup("s1");      // populates the cache
  clock.t = 1000 + 61;  // past the cache TTL (60s)
  db.lastGetColl.clear();
  sm.lookup("s1");
  EXPECT_EQ(db.lastGetColl, "sessions")
      << "an expired cache entry must fall through and re-query the DB";
}

TEST(SessionManagerTest, Revoke_PurgesCacheEntry) {
  MockMongodbClient db;
  FakeClock clock;
  clock.t = 1000;
  db.getDocumentResult =
      R"({"_id":"s1","accountCode":"a","role":"r",)"
      R"("authMethod":"password","expiresAt":999999999})";
  sso::SessionManager sm(db, clock);

  ASSERT_TRUE(sm.lookup("s1").valid);  // caches a valid session
  sm.revoke("s1");                     // deletes + purges the cache entry
  db.getDocumentResult = "";           // DB no longer has the session
  EXPECT_FALSE(sm.lookup("s1").valid)
      << "after revoke, lookup must miss the cache and re-query the DB";
}

// ═══════════════════════════════════════════════════════════════════════════════
// Phase B — outbound HTTP client (IHttpClient + encode_form)
// ═══════════════════════════════════════════════════════════════════════════════

TEST(HttpClientTest, FormEncode_EscapesReservedChars) {
  const std::string out = sso::encode_form({{"a", "x y"}, {"b", "p&q"}});
  EXPECT_EQ(out, "a=x%20y&b=p%26q");
}

TEST(HttpClientTest, FormEncode_EmptyMap_ReturnsEmpty) {
  EXPECT_TRUE(sso::encode_form({}).empty());
}

TEST(MockHttpClientTest, Get_ReturnsCannedResponse_ForUrl) {
  MockHttpClient http;
  sso::HttpResponse r;
  r.status = 200;
  r.body   = "hello";
  http.responses["https://idp.example/x"] = r;

  const sso::HttpResponse got = http.get("https://idp.example/x");
  EXPECT_EQ(got.status, 200);
  EXPECT_EQ(got.body, "hello");
  EXPECT_EQ(http.lastGetUrl, "https://idp.example/x");
}

TEST(MockHttpClientTest, PostForm_RecordsRequestBody) {
  MockHttpClient http;
  http.post_form("https://idp.example/token",
                 {{"grant_type", "authorization_code"}, {"code", "abc"}});
  EXPECT_EQ(http.lastPostUrl, "https://idp.example/token");
  EXPECT_NE(http.lastPostBody.find("grant_type=authorization_code"),
            std::string::npos);
  EXPECT_NE(http.lastPostBody.find("code=abc"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Phase C.1 — PKCE (RFC 7636)
// ═══════════════════════════════════════════════════════════════════════════════

TEST(PkceTest, Verifier_IsHighEntropy_UrlSafe) {
  const std::string v = sso::make_code_verifier();
  EXPECT_GE(v.size(), 43u);
  EXPECT_LE(v.size(), 128u);
  for (char c : v) {
    const bool unreserved = std::isalnum(static_cast<unsigned char>(c)) ||
                            c == '-' || c == '.' || c == '_' || c == '~';
    EXPECT_TRUE(unreserved) << "non-unreserved char in verifier: " << c;
  }
}

TEST(PkceTest, Challenge_IsS256OfVerifier_Base64Url) {
  const std::string verifier = "a-fixed-test-verifier-value";
  const std::string ch = sso::code_challenge(verifier);
  // base64url(SHA-256) → 43 chars, no padding, URL-safe alphabet.
  EXPECT_EQ(ch.size(), 43u);
  EXPECT_EQ(ch.find('='), std::string::npos);
  EXPECT_EQ(ch.find('+'), std::string::npos);
  EXPECT_EQ(ch.find('/'), std::string::npos);
  EXPECT_EQ(ch, sso::code_challenge(verifier));  // deterministic
}

TEST(PkceTest, Verifier_DiffersEachCall) {
  EXPECT_NE(sso::make_code_verifier(), sso::make_code_verifier());
}

// ═══════════════════════════════════════════════════════════════════════════════
// Phase C.3 — JWKS parsing
// ═══════════════════════════════════════════════════════════════════════════════

TEST(JwksTest, Parse_RsaKey_ExposesByKid) {
  sso::EvpPkeyPtr key = generate_rsa();
  ASSERT_TRUE(key);
  sso::Jwks j;
  ASSERT_TRUE(j.parse(make_jwks(key.get(), "abc")));
  EXPECT_EQ(j.size(), 1u);
  EXPECT_TRUE(j.has_key("abc"));
  EXPECT_FALSE(j.has_key("nope"));
}

TEST(JwksTest, Parse_MultipleKeys_AllIndexed) {
  sso::EvpPkeyPtr k1 = generate_rsa();
  sso::EvpPkeyPtr k2 = generate_rsa();
  ASSERT_TRUE(k1 && k2);
  const std::string doc =
      nlohmann::json{{"keys", nlohmann::json::array({jwk_of(k1.get(), "a"),
                                                     jwk_of(k2.get(), "b")})}}
          .dump();
  sso::Jwks j;
  ASSERT_TRUE(j.parse(doc));
  EXPECT_EQ(j.size(), 2u);
  EXPECT_TRUE(j.has_key("a"));
  EXPECT_TRUE(j.has_key("b"));
}

TEST(JwksTest, Parse_MalformedJwks_ReturnsError) {
  sso::Jwks j;
  EXPECT_FALSE(j.parse("{not json"));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Phase C.2 — RS256 id_token verification
// ═══════════════════════════════════════════════════════════════════════════════

class JwtVerifyTest : public ::testing::Test {
protected:
  sso::EvpPkeyPtr   m_key;
  sso::Jwks         m_jwks;
  sso::JwtExpect    m_expect;
  std::int64_t      m_now = 1000000;
  const std::string m_kid = "test-key-1";

  void SetUp() override {
    m_key = generate_rsa();
    ASSERT_TRUE(m_key) << "RSA keygen failed";
    ASSERT_TRUE(m_jwks.parse(make_jwks(m_key.get(), m_kid)));
    m_expect.issuer    = "https://idp.test";
    m_expect.client_id = "client-123";
    m_expect.nonce     = "n-abc";
  }

  nlohmann::json valid_header() const {
    return {{"alg", "RS256"}, {"kid", m_kid}, {"typ", "JWT"}};
  }
  nlohmann::json valid_payload() const {
    return {{"iss", "https://idp.test"},
            {"aud", "client-123"},
            {"exp", m_now + 3600},
            {"iat", m_now},
            {"nonce", "n-abc"},
            {"sub", "user|1"},
            {"email", "u@acme.com"},
            {"email_verified", true},
            {"name", "Test User"},
            {"groups", nlohmann::json::array({"g1", "g2"})}};
  }
};

TEST_F(JwtVerifyTest, ValidRs256Token_Verifies) {
  const std::string tok =
      sign_jwt(m_key.get(), valid_header(), valid_payload());
  const sso::JwtResult r = sso::verify_jwt(tok, m_jwks, m_expect, m_now);
  EXPECT_TRUE(r.ok) << r.error;
}

TEST_F(JwtVerifyTest, TamperedPayload_Rejected) {
  std::string tok = sign_jwt(m_key.get(), valid_header(), valid_payload());
  const std::size_t d1 = tok.find('.');  // flip a char inside the payload
  tok[d1 + 3] = (tok[d1 + 3] == 'A') ? 'B' : 'A';
  EXPECT_FALSE(sso::verify_jwt(tok, m_jwks, m_expect, m_now).ok);
}

TEST_F(JwtVerifyTest, WrongSigningKey_Rejected) {
  sso::EvpPkeyPtr other = generate_rsa();
  ASSERT_TRUE(other);
  // Signed by `other`, but the header's kid points at the key in m_jwks.
  const std::string tok =
      sign_jwt(other.get(), valid_header(), valid_payload());
  EXPECT_FALSE(sso::verify_jwt(tok, m_jwks, m_expect, m_now).ok);
}

TEST_F(JwtVerifyTest, AlgNone_Rejected) {
  nlohmann::json h = valid_header();
  h["alg"] = "none";
  const std::string tok = sign_jwt(m_key.get(), h, valid_payload());
  EXPECT_FALSE(sso::verify_jwt(tok, m_jwks, m_expect, m_now).ok);
}

TEST_F(JwtVerifyTest, AlgConfusion_Hs256SignedWithRsaPublicKey_Rejected) {
  nlohmann::json h = valid_header();
  h["alg"] = "HS256";  // verify_jwt rejects on alg — no HMAC path exists
  const std::string tok = sign_jwt(m_key.get(), h, valid_payload());
  EXPECT_FALSE(sso::verify_jwt(tok, m_jwks, m_expect, m_now).ok);
}

TEST_F(JwtVerifyTest, MissingKid_Rejected) {
  nlohmann::json h = valid_header();
  h.erase("kid");
  const std::string tok = sign_jwt(m_key.get(), h, valid_payload());
  EXPECT_FALSE(sso::verify_jwt(tok, m_jwks, m_expect, m_now).ok);
}

TEST_F(JwtVerifyTest, UnknownKid_Rejected) {
  nlohmann::json h = valid_header();
  h["kid"] = "some-other-kid";
  const std::string tok = sign_jwt(m_key.get(), h, valid_payload());
  EXPECT_FALSE(sso::verify_jwt(tok, m_jwks, m_expect, m_now).ok);
}

TEST_F(JwtVerifyTest, ExpiredToken_Rejected) {
  nlohmann::json p = valid_payload();
  p["exp"] = m_now - 3600;
  const std::string tok = sign_jwt(m_key.get(), valid_header(), p);
  EXPECT_FALSE(sso::verify_jwt(tok, m_jwks, m_expect, m_now).ok);
}

TEST_F(JwtVerifyTest, FutureIatBeyondSkew_Rejected) {
  nlohmann::json p = valid_payload();
  p["iat"] = m_now + 3600;
  const std::string tok = sign_jwt(m_key.get(), valid_header(), p);
  EXPECT_FALSE(sso::verify_jwt(tok, m_jwks, m_expect, m_now).ok);
}

TEST_F(JwtVerifyTest, WrongIssuer_Rejected) {
  nlohmann::json p = valid_payload();
  p["iss"] = "https://evil.test";
  const std::string tok = sign_jwt(m_key.get(), valid_header(), p);
  EXPECT_FALSE(sso::verify_jwt(tok, m_jwks, m_expect, m_now).ok);
}

TEST_F(JwtVerifyTest, AudienceMissingClientId_Rejected) {
  nlohmann::json p = valid_payload();
  p["aud"] = "some-other-client";
  const std::string tok = sign_jwt(m_key.get(), valid_header(), p);
  EXPECT_FALSE(sso::verify_jwt(tok, m_jwks, m_expect, m_now).ok);
}

TEST_F(JwtVerifyTest, MultiAudience_WrongAzp_Rejected) {
  nlohmann::json p = valid_payload();
  p["aud"] = nlohmann::json::array({"client-123", "another-client"});
  p["azp"] = "another-client";  // azp must equal our client_id
  const std::string tok = sign_jwt(m_key.get(), valid_header(), p);
  EXPECT_FALSE(sso::verify_jwt(tok, m_jwks, m_expect, m_now).ok);
}

TEST_F(JwtVerifyTest, NonceMismatch_Rejected) {
  nlohmann::json p = valid_payload();
  p["nonce"] = "different-nonce";
  const std::string tok = sign_jwt(m_key.get(), valid_header(), p);
  EXPECT_FALSE(sso::verify_jwt(tok, m_jwks, m_expect, m_now).ok);
}

TEST_F(JwtVerifyTest, ValidToken_ClaimsExtracted) {
  const std::string tok =
      sign_jwt(m_key.get(), valid_header(), valid_payload());
  const sso::JwtResult r = sso::verify_jwt(tok, m_jwks, m_expect, m_now);
  ASSERT_TRUE(r.ok) << r.error;
  EXPECT_EQ(r.subject, "user|1");
  EXPECT_EQ(r.email, "u@acme.com");
  EXPECT_TRUE(r.email_verified);
  EXPECT_EQ(r.name, "Test User");
  ASSERT_EQ(r.groups.size(), 2u);
  EXPECT_EQ(r.groups[0], "g1");
}

// ═══════════════════════════════════════════════════════════════════════════════
// Phase C.4 — OIDC discovery
// ═══════════════════════════════════════════════════════════════════════════════

TEST(OidcDiscoveryTest, Fetch_PopulatesEndpoints) {
  MockHttpClient http;
  const std::string issuer = "https://acme.okta.com/oauth2/default";
  const std::string disco_url =
      issuer + "/.well-known/openid-configuration";

  sso::HttpResponse r;
  r.status = 200;
  r.body   = nlohmann::json{
      {"issuer", issuer},
      {"authorization_endpoint", issuer + "/v1/authorize"},
      {"token_endpoint", issuer + "/v1/token"},
      {"jwks_uri", issuer + "/v1/keys"},
      {"end_session_endpoint", issuer + "/v1/logout"}}.dump();
  http.responses[disco_url] = r;

  sso::OidcEndpoints ep;
  std::string err;
  ASSERT_TRUE(sso::fetch_discovery(http, issuer, ep, err)) << err;
  EXPECT_EQ(ep.authorization_endpoint, issuer + "/v1/authorize");
  EXPECT_EQ(ep.token_endpoint, issuer + "/v1/token");
  EXPECT_EQ(ep.jwks_uri, issuer + "/v1/keys");
  EXPECT_EQ(ep.end_session_endpoint, issuer + "/v1/logout");
  EXPECT_EQ(http.lastGetUrl, disco_url);
}

TEST(OidcDiscoveryTest, Fetch_MalformedDoc_ReturnsError) {
  MockHttpClient http;
  const std::string issuer = "https://idp.test";
  sso::HttpResponse r;
  r.status = 200;
  r.body   = "{not json";
  http.responses[issuer + "/.well-known/openid-configuration"] = r;

  sso::OidcEndpoints ep;
  std::string err;
  EXPECT_FALSE(sso::fetch_discovery(http, issuer, ep, err));
}

TEST(OidcDiscoveryTest, Fetch_MissingRequiredEndpoint_ReturnsError) {
  MockHttpClient http;
  const std::string issuer = "https://idp.test";
  sso::HttpResponse r;
  r.status = 200;
  r.body   = nlohmann::json{
      {"issuer", issuer},
      {"authorization_endpoint", issuer + "/authorize"},
      {"jwks_uri", issuer + "/keys"}}.dump();  // token_endpoint missing
  http.responses[issuer + "/.well-known/openid-configuration"] = r;

  sso::OidcEndpoints ep;
  std::string err;
  EXPECT_FALSE(sso::fetch_discovery(http, issuer, ep, err));
}

TEST(OidcDiscoveryTest, Fetch_IssuerMismatch_ReturnsError) {
  MockHttpClient http;
  const std::string issuer = "https://idp.test";
  sso::HttpResponse r;
  r.status = 200;
  r.body   = nlohmann::json{
      {"issuer", "https://evil.test"},  // does not match the configured issuer
      {"authorization_endpoint", issuer + "/authorize"},
      {"token_endpoint", issuer + "/token"},
      {"jwks_uri", issuer + "/keys"}}.dump();
  http.responses[issuer + "/.well-known/openid-configuration"] = r;

  sso::OidcEndpoints ep;
  std::string err;
  EXPECT_FALSE(sso::fetch_discovery(http, issuer, ep, err));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Phase C.4b — sso_config hot-reload
// ═══════════════════════════════════════════════════════════════════════════════

TEST(ConfigReloadTest, ChangedConfig_RebuildsRegistry) {
  sso::ProviderRegistry reg;
  const char *cfg_a =
      R"({"publicBaseUrl":"https://x","providers":[{"id":"a","protocol":"oidc"}]})";
  const char *cfg_b =
      R"({"publicBaseUrl":"https://x","providers":[{"id":"b","protocol":"oidc"}]})";

  EXPECT_TRUE(reg.reload_if_changed(cfg_a));
  EXPECT_TRUE(reg.reload_if_changed(cfg_b));
  EXPECT_NE(reg.find("b"), nullptr);
  EXPECT_EQ(reg.find("a"), nullptr);  // config B replaced config A
}

TEST(ConfigReloadTest, UnchangedConfig_DoesNotRebuild) {
  sso::ProviderRegistry reg;
  const char *cfg =
      R"({"publicBaseUrl":"https://x","providers":[{"id":"a","protocol":"oidc"}]})";

  EXPECT_TRUE(reg.reload_if_changed(cfg));
  EXPECT_FALSE(reg.reload_if_changed(cfg));  // identical document → no rebuild
}

TEST(ConfigReloadTest, InvalidConfig_KeepsLastGood) {
  sso::ProviderRegistry reg;
  const char *good =
      R"({"publicBaseUrl":"https://x","providers":[{"id":"a","protocol":"oidc"}]})";

  ASSERT_TRUE(reg.reload_if_changed(good));
  EXPECT_FALSE(reg.reload_if_changed("{ not valid json"));
  // The last-good configuration is still in effect.
  EXPECT_NE(reg.find("a"), nullptr);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Phase C.5 — OidcProvider::begin_login
// ═══════════════════════════════════════════════════════════════════════════════

class OidcProviderTest : public ::testing::Test {
protected:
  MockMongodbClient                  m_db;
  MockHttpClient                     m_http;
  FakeClock                          m_clock;
  sso::EvpPkeyPtr                    m_key;
  std::unique_ptr<sso::OidcProvider> m_provider;

  const std::int64_t m_now   = 1000000;
  const std::string  m_kid   = "oidc-key";
  const std::string  m_state = "txn-state-1";
  const std::string  m_nonce = "nonce-xyz";

  void SetUp() override {
    m_clock.t = m_now;
    m_key     = generate_rsa();
    ASSERT_TRUE(m_key) << "RSA keygen failed";

    sso::ProviderConfig cfg;
    cfg.id            = "corp";
    cfg.client_id     = "client-123";
    cfg.client_secret = "secret-xyz";
    cfg.issuer        = "https://idp.test";
    cfg.scopes        = {"openid", "email", "profile"};

    sso::OidcEndpoints ep;
    ep.issuer                 = "https://idp.test";
    ep.authorization_endpoint = "https://idp.test/authorize";
    ep.token_endpoint         = "https://idp.test/token";
    ep.jwks_uri               = "https://idp.test/keys";

    m_provider = std::make_unique<sso::OidcProvider>(
        cfg, ep, "https://marvel-3a78bd953f5f.herokuapp.com", m_db, m_http,
        m_clock);

    // The transaction handle_callback() will read.
    m_db.getDocumentResult = nlohmann::json{{"_id", m_state},
                                            {"provider", "corp"},
                                            {"nonce", m_nonce},
                                            {"codeVerifier", "verifier-abc"},
                                            {"returnTo", "/main"}}
                                 .dump();
    // The JWKS the IdP serves at jwks_uri.
    sso::HttpResponse jwks_resp;
    jwks_resp.status = 200;
    jwks_resp.body   = make_jwks(m_key.get(), m_kid);
    m_http.responses["https://idp.test/keys"] = jwks_resp;
  }

  nlohmann::json id_header() const {
    return {{"alg", "RS256"}, {"kid", m_kid}, {"typ", "JWT"}};
  }
  nlohmann::json id_payload() const {
    return {{"iss", "https://idp.test"},
            {"aud", "client-123"},
            {"exp", m_now + 3600},
            {"iat", m_now},
            {"nonce", m_nonce},
            {"sub", "user|9"},
            {"email", "x@acme.com"},
            {"email_verified", true},
            {"name", "X User"},
            {"groups", nlohmann::json::array({"g1"})}};
  }
  // Arrange a token-endpoint response carrying `id_token`.
  void set_token_response(const std::string &id_token) {
    sso::HttpResponse r;
    r.status = 200;
    r.body   = nlohmann::json{{"access_token", "at"},
                              {"token_type", "Bearer"},
                              {"id_token", id_token}}
                   .dump();
    m_http.responses["https://idp.test/token"] = r;
  }
};

TEST_F(OidcProviderTest, BuildsAuthorizeUrl_WithAllParams) {
  const sso::AuthnRequest req = m_provider->begin_login("/main");
  const std::string &u = req.redirect_url;
  EXPECT_NE(u.find("https://idp.test/authorize?"), std::string::npos);
  EXPECT_NE(u.find("response_type=code"), std::string::npos);
  EXPECT_NE(u.find("client_id=client-123"), std::string::npos);
  EXPECT_NE(u.find("scope="), std::string::npos);
  EXPECT_NE(u.find("state="), std::string::npos);
  EXPECT_NE(u.find("nonce="), std::string::npos);
  EXPECT_NE(u.find("code_challenge="), std::string::npos);
  EXPECT_NE(u.find("code_challenge_method=S256"), std::string::npos);
  EXPECT_FALSE(req.transaction_id.empty());
}

TEST_F(OidcProviderTest, PersistsTransaction_WithStateNonceVerifier) {
  const sso::AuthnRequest req = m_provider->begin_login("/dashboard");
  EXPECT_EQ(m_db.lastCreateColl, "sso_transactions");

  const nlohmann::json doc = nlohmann::json::parse(m_db.lastCreateDoc);
  EXPECT_EQ(doc.value("_id", std::string{}), req.transaction_id);  // _id == state
  EXPECT_EQ(doc.value("provider", std::string{}), "corp");
  EXPECT_FALSE(doc.value("nonce", std::string{}).empty());
  EXPECT_FALSE(doc.value("codeVerifier", std::string{}).empty());
  EXPECT_EQ(doc.value("returnTo", std::string{}), "/dashboard");
}

TEST_F(OidcProviderTest, RedirectUri_PinnedToPublicBaseUrl) {
  // begin_login takes no request — the redirect_uri can only come from the
  // configured public base URL, never a Host header.
  const sso::AuthnRequest req = m_provider->begin_login("/main");
  EXPECT_NE(req.redirect_url.find("marvel-3a78bd953f5f.herokuapp.com"),
            std::string::npos);
  EXPECT_NE(req.redirect_url.find("callback"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Phase C.6 — OidcProvider::handle_callback
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(OidcProviderTest, ValidCode_ExchangesAndReturnsClaims) {
  set_token_response(sign_jwt(m_key.get(), id_header(), id_payload()));
  const sso::IdentityClaims r =
      m_provider->handle_callback("auth-code", m_state);
  ASSERT_TRUE(r.ok) << r.error;
  EXPECT_EQ(r.subject, "user|9");
  EXPECT_EQ(r.email, "x@acme.com");
  EXPECT_TRUE(r.email_verified);
  EXPECT_EQ(r.return_to, "/main");
}

TEST_F(OidcProviderTest, Transaction_ClaimedAtomically) {
  set_token_response(sign_jwt(m_key.get(), id_header(), id_payload()));
  m_provider->handle_callback("auth-code", m_state);
  EXPECT_EQ(m_db.lastUpdateColl, "sso_transactions");
  EXPECT_NE(m_db.lastUpdateDoc.find("consumed"), std::string::npos);
}

TEST_F(OidcProviderTest, UnknownState_Rejected) {
  m_db.getDocumentResult = "";  // no such transaction
  const sso::IdentityClaims r =
      m_provider->handle_callback("auth-code", "no-such-state");
  EXPECT_FALSE(r.ok);
}

TEST_F(OidcProviderTest, ReplayedState_SecondCallRejected) {
  set_token_response(sign_jwt(m_key.get(), id_header(), id_payload()));
  EXPECT_TRUE(m_provider->handle_callback("auth-code", m_state).ok);
  // The transaction is now consumed — a replay must fail.
  EXPECT_FALSE(m_provider->handle_callback("auth-code", m_state).ok);
}

TEST_F(OidcProviderTest, TokenEndpointError_ReturnsFailure) {
  sso::HttpResponse err;
  err.status = 500;
  m_http.responses["https://idp.test/token"] = err;
  const sso::IdentityClaims r =
      m_provider->handle_callback("auth-code", m_state);
  EXPECT_FALSE(r.ok);
}

TEST_F(OidcProviderTest, IdTokenVerificationFails_ReturnsFailure) {
  // id_token signed by a key that is not in the IdP's JWKS.
  sso::EvpPkeyPtr other = generate_rsa();
  ASSERT_TRUE(other);
  set_token_response(sign_jwt(other.get(), id_header(), id_payload()));
  const sso::IdentityClaims r =
      m_provider->handle_callback("auth-code", m_state);
  EXPECT_FALSE(r.ok);
}

TEST_F(OidcProviderTest, NonceMismatchVsTransaction_Rejected) {
  nlohmann::json p = id_payload();
  p["nonce"] = "a-different-nonce";  // not the transaction's nonce
  set_token_response(sign_jwt(m_key.get(), id_header(), p));
  const sso::IdentityClaims r =
      m_provider->handle_callback("auth-code", m_state);
  EXPECT_FALSE(r.ok);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Phase C.7 — hybrid account provisioning
// ═══════════════════════════════════════════════════════════════════════════════

class ProvisioningTest : public ::testing::Test {
protected:
  MockMongodbClient   m_db;
  sso::ProviderConfig m_provider;
  sso::IdentityClaims m_claims;

  void SetUp() override {
    m_provider.id                   = "corp";
    m_provider.default_role          = "Customer";
    m_provider.allowed_email_domains = {"acme.com"};

    m_claims.ok             = true;
    m_claims.subject        = "idp|user-1";
    m_claims.email          = "jane@acme.com";
    m_claims.email_verified = true;
    m_claims.display_name   = "Jane";
  }

  // An existing account document, role "Employee".
  static std::string account_doc() {
    return nlohmann::json{{"loginCredentials", {{"accountCode", "acme-jane"}}},
                          {"personalInfo", {{"role", "Employee"}}}}
        .dump();
  }
};

TEST_F(ProvisioningTest, SubjectMatch_UsesLinkedAccount) {
  m_db.accountBySubject = account_doc();
  const sso::ResolvedAccount r =
      sso::resolve_account(m_db, m_provider, m_claims);
  ASSERT_TRUE(r.ok) << r.error;
  EXPECT_EQ(r.account_code, "acme-jane");
}

TEST_F(ProvisioningTest, EmailMatch_VerifiedEmail_LinksAndUsesAccount) {
  m_db.accountBySubject = "";              // not yet linked
  m_db.accountByEmail   = account_doc();   // but a verified-email match exists
  const sso::ResolvedAccount r =
      sso::resolve_account(m_db, m_provider, m_claims);
  ASSERT_TRUE(r.ok) << r.error;
  EXPECT_EQ(r.account_code, "acme-jane");
  // The SSO identity was linked onto the matched account.
  EXPECT_EQ(m_db.lastUpdateColl, "account");
  EXPECT_NE(m_db.lastUpdateDoc.find("ssoIdentities"), std::string::npos);
}

TEST_F(ProvisioningTest, EmailMatch_UnverifiedEmail_FallsThroughToJit) {
  m_db.accountBySubject   = "";
  m_db.accountByEmail     = account_doc();
  m_claims.email_verified = false;  // email-match path requires verified email
  const sso::ResolvedAccount r =
      sso::resolve_account(m_db, m_provider, m_claims);
  ASSERT_TRUE(r.ok) << r.error;
  EXPECT_EQ(m_db.lastCreateColl, "account");  // JIT-created, not matched
}

TEST_F(ProvisioningTest, EmailOutsideAllowedDomains_Rejected) {
  m_db.accountBySubject = "";
  m_claims.email        = "jane@evil.com";  // outside allowed_email_domains
  const sso::ResolvedAccount r =
      sso::resolve_account(m_db, m_provider, m_claims);
  EXPECT_FALSE(r.ok);
  EXPECT_TRUE(m_db.lastCreateColl.empty());  // no match, no JIT
}

TEST_F(ProvisioningTest, NoMatch_JitCreatesAccount_WithDefaultRole) {
  m_db.accountBySubject = "";
  m_db.accountByEmail   = "";
  const sso::ResolvedAccount r =
      sso::resolve_account(m_db, m_provider, m_claims);
  ASSERT_TRUE(r.ok) << r.error;
  EXPECT_EQ(m_db.lastCreateColl, "account");
  EXPECT_EQ(r.role, "Customer");  // the provider default
  const nlohmann::json doc = nlohmann::json::parse(m_db.lastCreateDoc);
  EXPECT_EQ(doc["personalInfo"]["role"], "Customer");
}

TEST_F(ProvisioningTest, Jit_GroupRoleMap_MapsGroupToRole) {
  m_db.accountBySubject = "";
  m_db.accountByEmail   = "";
  m_provider.group_role_map_enabled = true;
  m_provider.group_role_map         = {{"xpmile-admins", "Admin"}};
  m_claims.groups                   = {"xpmile-admins"};
  const sso::ResolvedAccount r =
      sso::resolve_account(m_db, m_provider, m_claims);
  ASSERT_TRUE(r.ok) << r.error;
  EXPECT_EQ(r.role, "Admin");
}

TEST_F(ProvisioningTest, MatchedAccount_KeepsExistingDbRole) {
  m_db.accountBySubject = account_doc();  // existing role is "Employee"
  // Even a group that would map to Admin must not override a matched account.
  m_provider.group_role_map_enabled = true;
  m_provider.group_role_map         = {{"some-group", "Admin"}};
  m_claims.groups                   = {"some-group"};
  const sso::ResolvedAccount r =
      sso::resolve_account(m_db, m_provider, m_claims);
  ASSERT_TRUE(r.ok) << r.error;
  EXPECT_EQ(r.role, "Employee");
}

// ═══════════════════════════════════════════════════════════════════════════════
// Phase C.8 — SSO endpoint logic
// ═══════════════════════════════════════════════════════════════════════════════

TEST(SsoEndpointTest, GetProviders_ReturnsConfiguredList) {
  sso::ProviderRegistry reg;
  ASSERT_TRUE(reg.reload_if_changed(
      R"({"publicBaseUrl":"https://x.test","providers":[)"
      R"({"id":"alpha","displayName":"Alpha","protocol":"oidc"},)"
      R"({"id":"beta","displayName":"Beta","protocol":"saml"}]})"));
  const sso::SsoHttpResult r = sso::sso_list_providers(reg.config());
  EXPECT_EQ(r.status, 200);
  const nlohmann::json body = nlohmann::json::parse(r.body);
  ASSERT_EQ(body.size(), 2u);
  EXPECT_EQ(body[0]["id"], "alpha");
  EXPECT_EQ(body[0]["protocol"], "oidc");
  EXPECT_EQ(body[1]["protocol"], "saml");
}

TEST(SsoEndpointTest, Login_RedirectsToProvider_302) {
  MockIdentityProvider p;
  p.beginResult.redirect_url = "https://idp.test/authorize?client_id=x";
  const sso::SsoHttpResult r = sso::sso_begin_login(&p, "/main");
  EXPECT_EQ(r.status, 302);
  EXPECT_EQ(r.location, "https://idp.test/authorize?client_id=x");
}

TEST(SsoEndpointTest, Login_UnknownProvider_400) {
  const sso::SsoHttpResult r = sso::sso_begin_login(nullptr, "/main");
  EXPECT_EQ(r.status, 400);
}

TEST(SsoEndpointTest, Login_NonLocalReturnTo_Rejected) {
  MockIdentityProvider p;
  // An absolute URL and a protocol-relative path are both open-redirect bait.
  EXPECT_EQ(sso::sso_begin_login(&p, "https://evil.test/x").status, 400);
  EXPECT_EQ(sso::sso_begin_login(&p, "//evil.test/x").status, 400);
}

TEST(SsoEndpointTest, Callback_SuccessfulClaims_CreatesSession_SetsCookie_302) {
  MockMongodbClient db;  // empty account lookups -> resolve_account JIT-creates
  FakeClock clock;
  sso::SessionManager sm(db, clock);

  MockIdentityProvider p;
  p.callbackResult.ok        = true;
  p.callbackResult.subject   = "idp|u1";
  p.callbackResult.email     = "u1@corp.test";
  p.callbackResult.return_to = "/dashboard";

  sso::ProviderConfig cfg;
  cfg.id           = "mock";
  cfg.default_role = "Customer";

  const sso::SsoHttpResult r =
      sso::sso_complete_callback(&p, &cfg, "code", "state", db, sm);
  EXPECT_EQ(r.status, 302);
  EXPECT_EQ(r.location, "/dashboard");
  EXPECT_NE(r.set_cookie.find("xpmile_session="), std::string::npos);
}

TEST(SsoEndpointTest, Callback_FailedClaims_NoSession_RedirectsToLoginError) {
  MockMongodbClient db;
  FakeClock clock;
  sso::SessionManager sm(db, clock);

  MockIdentityProvider p;
  p.callbackResult.ok = false;  // callback verification failed

  sso::ProviderConfig cfg;
  cfg.id = "mock";

  const sso::SsoHttpResult r =
      sso::sso_complete_callback(&p, &cfg, "code", "state", db, sm);
  EXPECT_EQ(r.status, 302);
  EXPECT_NE(r.location.find("/login"), std::string::npos);
  EXPECT_TRUE(r.set_cookie.empty());          // no session cookie
  EXPECT_TRUE(db.lastCreateColl.empty());     // no session document written
}

TEST(SsoEndpointTest, Session_ValidCookie_ReturnsAccount) {
  MockMongodbClient db;
  db.getDocumentResult =
      R"({"_id":"s1","accountCode":"acme-ops","role":"Admin",)"
      R"("authMethod":"oidc","expiresAt":9999999999})";
  FakeClock clock;
  sso::SessionManager sm(db, clock);

  const sso::SsoHttpResult r =
      sso::sso_session_info("xpmile_session=s1", sm);
  EXPECT_EQ(r.status, 200);
  const nlohmann::json body = nlohmann::json::parse(r.body);
  EXPECT_EQ(body["accountCode"], "acme-ops");
  EXPECT_EQ(body["role"], "Admin");
}

TEST(SsoEndpointTest, Session_NoCookie_Returns401) {
  MockMongodbClient db;
  FakeClock clock;
  sso::SessionManager sm(db, clock);
  EXPECT_EQ(sso::sso_session_info("", sm).status, 401);
}

TEST(SsoEndpointTest, Logout_DeletesSession_ClearsCookie) {
  MockMongodbClient db;
  FakeClock clock;
  sso::SessionManager sm(db, clock);

  const sso::SsoHttpResult r = sso::sso_logout("xpmile_session=s1", sm);
  EXPECT_EQ(db.lastDeleteColl, "sessions");  // the session was revoked
  EXPECT_NE(r.set_cookie.find("Max-Age=0"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Phase E.1 — SAML AuthnRequest
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

// Extract and percent-decode the SAMLRequest query parameter from a redirect.
std::string saml_request_param(const std::string &url) {
  const std::string key = "SAMLRequest=";
  std::size_t start = url.find(key);
  if (start == std::string::npos) return {};
  start += key.size();
  const std::size_t end = url.find('&', start);
  const std::string raw = url.substr(
      start, end == std::string::npos ? std::string::npos : end - start);

  std::string out;
  for (std::size_t i = 0; i < raw.size(); ++i) {
    if (raw[i] == '%' && i + 2 < raw.size()) {
      out += static_cast<char>(std::stoi(raw.substr(i + 1, 2), nullptr, 16));
      i += 2;
    } else {
      out += raw[i];
    }
  }
  return out;
}

} // namespace

class SamlAuthnRequestTest : public ::testing::Test {
protected:
  MockMongodbClient   m_db;
  FakeClock           m_clock;
  sso::ProviderConfig m_config;
  std::unique_ptr<sso::SamlProvider> m_provider;

  void SetUp() override {
    m_config.id           = "partner";
    m_config.protocol     = sso::Protocol::Saml;
    m_config.idp_sso_url  = "https://idp.partner.test/saml/sso";
    m_config.sp_entity_id = "xpmile-marvel";
    m_provider = std::make_unique<sso::SamlProvider>(
        m_config, "https://app.test", m_db, m_clock);
  }
};

TEST_F(SamlAuthnRequestTest, Build_ProducesDeflatedBase64) {
  const sso::AuthnRequest req = m_provider->begin_login("/main");
  const std::string encoded = saml_request_param(req.redirect_url);
  ASSERT_FALSE(encoded.empty());
  const std::string xml = sso::saml_inflate(sso::base64_decode(encoded));
  EXPECT_NE(xml.find("<samlp:AuthnRequest"), std::string::npos);
}

TEST_F(SamlAuthnRequestTest, Build_ContainsIssuerAndAcsUrl) {
  const sso::AuthnRequest req = m_provider->begin_login("/main");
  const std::string xml = sso::saml_inflate(
      sso::base64_decode(saml_request_param(req.redirect_url)));
  EXPECT_NE(xml.find("<saml:Issuer>xpmile-marvel</saml:Issuer>"),
            std::string::npos);
  EXPECT_NE(xml.find("AssertionConsumerServiceURL="
                     "\"https://app.test/api/v1/sso/callback/partner\""),
            std::string::npos);
}

TEST_F(SamlAuthnRequestTest, Build_PersistsTransaction_WithRequestId) {
  const sso::AuthnRequest req = m_provider->begin_login("/main");
  EXPECT_EQ(m_db.lastCreateColl, "sso_transactions");
  // The transaction is keyed by the AuthnRequest ID …
  EXPECT_NE(m_db.lastCreateDoc.find(req.transaction_id), std::string::npos);
  // … and RelayState carries it back on the callback.
  EXPECT_NE(req.redirect_url.find("RelayState=" + req.transaction_id),
            std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Phase E.2 — SAML response parse
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

const char *kSampleSamlResponse = R"SAML(<samlp:Response
  xmlns:samlp="urn:oasis:names:tc:SAML:2.0:protocol"
  xmlns:saml="urn:oasis:names:tc:SAML:2.0:assertion"
  ID="_resp1" Version="2.0">
  <saml:Issuer>https://idp.partner.test/saml</saml:Issuer>
  <samlp:Status>
    <samlp:StatusCode Value="urn:oasis:names:tc:SAML:2.0:status:Success"/>
  </samlp:Status>
  <saml:Assertion ID="_assert1" Version="2.0"
                  IssueInstant="2026-05-21T12:00:00Z">
    <saml:Issuer>https://idp.partner.test/saml</saml:Issuer>
    <saml:Subject>
      <saml:NameID>jane@partner.test</saml:NameID>
      <saml:SubjectConfirmation Method="urn:oasis:names:tc:SAML:2.0:cm:bearer">
        <saml:SubjectConfirmationData
          Recipient="https://app.test/api/v1/sso/callback/partner"
          NotOnOrAfter="2026-05-21T12:05:00Z"
          InResponseTo="_req1"/>
      </saml:SubjectConfirmation>
    </saml:Subject>
    <saml:Conditions NotBefore="2026-05-21T11:55:00Z"
                     NotOnOrAfter="2026-05-21T12:05:00Z">
      <saml:AudienceRestriction>
        <saml:Audience>xpmile-marvel</saml:Audience>
      </saml:AudienceRestriction>
    </saml:Conditions>
    <saml:AttributeStatement>
      <saml:Attribute Name="email">
        <saml:AttributeValue>jane@partner.test</saml:AttributeValue>
      </saml:Attribute>
      <saml:Attribute Name="displayName">
        <saml:AttributeValue>Jane Partner</saml:AttributeValue>
      </saml:Attribute>
      <saml:Attribute Name="groups">
        <saml:AttributeValue>partner-users</saml:AttributeValue>
        <saml:AttributeValue>xpmile-admins</saml:AttributeValue>
      </saml:Attribute>
    </saml:AttributeStatement>
  </saml:Assertion>
</samlp:Response>)SAML";

} // namespace

TEST(SamlParseTest, DecodeBase64Response_ExtractsAssertion) {
  const sso::SamlResponse r =
      sso::parse_saml_response(sso::base64_encode(kSampleSamlResponse));
  ASSERT_TRUE(r.ok) << r.error;

  const sso::SamlAssertion &a = r.assertion;
  EXPECT_EQ(a.id, "_assert1");
  EXPECT_EQ(a.issuer, "https://idp.partner.test/saml");
  EXPECT_EQ(a.subject_name_id, "jane@partner.test");
  EXPECT_EQ(a.audience, "xpmile-marvel");
  EXPECT_EQ(a.not_before, "2026-05-21T11:55:00Z");
  EXPECT_EQ(a.not_on_or_after, "2026-05-21T12:05:00Z");
  EXPECT_EQ(a.recipient, "https://app.test/api/v1/sso/callback/partner");
  EXPECT_EQ(a.in_response_to, "_req1");
  EXPECT_EQ(a.email, "jane@partner.test");
  EXPECT_EQ(a.display_name, "Jane Partner");
  ASSERT_EQ(a.groups.size(), 2u);
  EXPECT_EQ(a.groups[0], "partner-users");
  EXPECT_EQ(a.groups[1], "xpmile-admins");
}

TEST(SamlParseTest, MalformedXml_ReturnsError) {
  const sso::SamlResponse r =
      sso::parse_saml_response(sso::base64_encode("<samlp:Response><broken"));
  EXPECT_FALSE(r.ok);
  EXPECT_FALSE(r.error.empty());
}

TEST(SamlParseTest, MissingAssertion_ReturnsError) {
  const char *no_assertion =
      "<samlp:Response xmlns:samlp=\"urn:oasis:names:tc:SAML:2.0:protocol\">"
      "<samlp:Status/></samlp:Response>";
  const sso::SamlResponse r =
      sso::parse_saml_response(sso::base64_encode(no_assertion));
  EXPECT_FALSE(r.ok);
  EXPECT_NE(r.error.find("assertion"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Phase E.3 — SAML XML-DSig verification
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

// A SAMLResponse carrying an empty enveloped <ds:Signature> template inside
// the assertion. saml_sign() fills DigestValue + SignatureValue.
const char *kSamlSignTemplate = R"SAML(<samlp:Response
  xmlns:samlp="urn:oasis:names:tc:SAML:2.0:protocol"
  xmlns:saml="urn:oasis:names:tc:SAML:2.0:assertion"
  ID="_resp1" Version="2.0">
  <saml:Issuer>https://idp.partner.test/saml</saml:Issuer>
  <saml:Assertion ID="_assert1" Version="2.0"
                  IssueInstant="2026-05-21T12:00:00Z">
    <saml:Issuer>https://idp.partner.test/saml</saml:Issuer>
    <ds:Signature xmlns:ds="http://www.w3.org/2000/09/xmldsig#">
      <ds:SignedInfo>
        <ds:CanonicalizationMethod
          Algorithm="http://www.w3.org/2001/10/xml-exc-c14n#"/>
        <ds:SignatureMethod
          Algorithm="http://www.w3.org/2001/04/xmldsig-more#rsa-sha256"/>
        <ds:Reference URI="#_assert1">
          <ds:Transforms>
            <ds:Transform
              Algorithm="http://www.w3.org/2000/09/xmldsig#enveloped-signature"/>
            <ds:Transform Algorithm="http://www.w3.org/2001/10/xml-exc-c14n#"/>
          </ds:Transforms>
          <ds:DigestMethod Algorithm="http://www.w3.org/2001/04/xmlenc#sha256"/>
          <ds:DigestValue></ds:DigestValue>
        </ds:Reference>
      </ds:SignedInfo>
      <ds:SignatureValue></ds:SignatureValue>
    </ds:Signature>
    <saml:Subject>
      <saml:NameID>jane@partner.test</saml:NameID>
    </saml:Subject>
    <saml:Conditions NotBefore="2026-05-21T11:55:00Z"
                     NotOnOrAfter="2026-05-21T12:05:00Z">
      <saml:AudienceRestriction>
        <saml:Audience>xpmile-marvel</saml:Audience>
      </saml:AudienceRestriction>
    </saml:Conditions>
  </saml:Assertion>
</samlp:Response>)SAML";

std::string priv_key_pem(EVP_PKEY *key) {
  BIO *bio = BIO_new(BIO_s_mem());
  PEM_write_bio_PrivateKey(bio, key, nullptr, nullptr, 0, nullptr, nullptr);
  char *data = nullptr;
  const long len = BIO_get_mem_data(bio, &data);
  std::string out(data, static_cast<std::size_t>(len));
  BIO_free(bio);
  return out;
}

// A minimal self-signed X.509 certificate wrapping @p key — stands in for the
// IdP's signing certificate.
std::string make_cert_pem(EVP_PKEY *key) {
  X509 *x = X509_new();
  ASN1_INTEGER_set(X509_get_serialNumber(x), 1);
  X509_gmtime_adj(X509_getm_notBefore(x), 0);
  X509_gmtime_adj(X509_getm_notAfter(x), 31536000L);
  X509_set_pubkey(x, key);
  X509_NAME *name = X509_get_subject_name(x);
  X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
      reinterpret_cast<const unsigned char *>("test-idp"), -1, -1, 0);
  X509_set_issuer_name(x, name);
  X509_sign(x, key, EVP_sha256());
  BIO *bio = BIO_new(BIO_s_mem());
  PEM_write_bio_X509(bio, x);
  char *data = nullptr;
  const long len = BIO_get_mem_data(bio, &data);
  std::string out(data, static_cast<std::size_t>(len));
  BIO_free(bio);
  X509_free(x);
  return out;
}

void reg_ids(xmlNodePtr node) {
  for (xmlNodePtr c = node; c != nullptr; c = c->next) {
    if (c->type != XML_ELEMENT_NODE) continue;
    xmlAttrPtr attr =
        xmlHasProp(c, reinterpret_cast<const xmlChar *>("ID"));
    if (attr != nullptr) {
      xmlChar *val = xmlNodeListGetString(c->doc, attr->children, 1);
      if (val != nullptr) {
        xmlAddID(nullptr, c->doc, val, attr);
        xmlFree(val);
      }
    }
    reg_ids(c->children);
  }
}

// Sign a SAMLResponse template with @p priv_pem — stands in for the IdP.
std::string saml_sign(const std::string &xml, const std::string &priv_pem) {
  if (!sso::saml_crypto_init()) return {};
  xmlDocPtr doc = xmlReadMemory(xml.data(), static_cast<int>(xml.size()),
                                "tmpl.xml", nullptr, 0);
  if (doc == nullptr) return {};
  reg_ids(xmlDocGetRootElement(doc));

  std::string out;
  xmlNodePtr sig = xmlSecFindNode(xmlDocGetRootElement(doc),
                                  xmlSecNodeSignature, xmlSecDSigNs);
  if (sig != nullptr) {
    xmlSecDSigCtxPtr ctx = xmlSecDSigCtxCreate(nullptr);
    if (ctx != nullptr) {
      ctx->signKey = xmlSecCryptoAppKeyLoadMemory(
          reinterpret_cast<const xmlSecByte *>(priv_pem.data()),
          priv_pem.size(), xmlSecKeyDataFormatPem, nullptr, nullptr, nullptr);
      if (ctx->signKey != nullptr && xmlSecDSigCtxSign(ctx, sig) == 0) {
        xmlChar *buf = nullptr;
        int n = 0;
        xmlDocDumpMemory(doc, &buf, &n);
        if (buf != nullptr) {
          out.assign(reinterpret_cast<const char *>(buf),
                     static_cast<std::size_t>(n));
          xmlFree(buf);
        }
      }
      xmlSecDSigCtxDestroy(ctx);
    }
  }
  xmlFreeDoc(doc);
  return out;
}

} // namespace

class SamlSignatureTest : public ::testing::Test {
protected:
  std::string m_signed;      // a validly signed SAMLResponse
  std::string m_cert;        // the matching IdP certificate (PEM)
  std::string m_wrong_cert;  // an unrelated certificate (PEM)

  void SetUp() override {
    sso::EvpPkeyPtr idp_key = generate_rsa();
    ASSERT_TRUE(idp_key);
    m_cert   = make_cert_pem(idp_key.get());
    m_signed = saml_sign(kSamlSignTemplate, priv_key_pem(idp_key.get()));

    sso::EvpPkeyPtr other_key = generate_rsa();
    ASSERT_TRUE(other_key);
    m_wrong_cert = make_cert_pem(other_key.get());
  }
};

TEST_F(SamlSignatureTest, ValidSignature_KnownGoodResponse_Verifies) {
  ASSERT_FALSE(m_signed.empty());
  const sso::SamlSignatureResult r =
      sso::verify_saml_signature(m_signed, m_cert);
  EXPECT_TRUE(r.ok) << r.error;
}

TEST_F(SamlSignatureTest, TamperedAssertion_Rejected) {
  ASSERT_FALSE(m_signed.empty());
  std::string tampered = m_signed;
  const std::size_t p = tampered.find("jane@partner.test");
  ASSERT_NE(p, std::string::npos);
  tampered[p] = 'X';  // one byte changed inside the signed assertion
  EXPECT_FALSE(sso::verify_saml_signature(tampered, m_cert).ok);
}

TEST_F(SamlSignatureTest, WrongSigningCert_Rejected) {
  ASSERT_FALSE(m_signed.empty());
  EXPECT_FALSE(sso::verify_saml_signature(m_signed, m_wrong_cert).ok);
}

TEST_F(SamlSignatureTest, UnsignedResponse_Rejected) {
  // kSampleSamlResponse (Phase E.2) carries no <Signature> at all.
  EXPECT_FALSE(sso::verify_saml_signature(kSampleSamlResponse, m_cert).ok);
}

TEST_F(SamlSignatureTest, SignatureWrappingAttack_Rejected) {
  ASSERT_FALSE(m_signed.empty());
  // Inject a forged second assertion ahead of the signed one — the classic
  // XML signature-wrapping shape.
  const std::string forged =
      "<saml:Assertion xmlns:saml=\"urn:oasis:names:tc:SAML:2.0:assertion\" "
      "ID=\"_forged\"><saml:Issuer>evil</saml:Issuer>"
      "<saml:Subject><saml:NameID>attacker@evil.test</saml:NameID>"
      "</saml:Subject></saml:Assertion>";
  std::string wrapped = m_signed;
  const std::size_t p = wrapped.find("<saml:Assertion");
  ASSERT_NE(p, std::string::npos);
  wrapped.insert(p, forged);
  EXPECT_FALSE(sso::verify_saml_signature(wrapped, m_cert).ok);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Phase E.4 — SAML assertion condition validation
// ═══════════════════════════════════════════════════════════════════════════════

class SamlConditionsTest : public ::testing::Test {
protected:
  // 2026-05-21T12:00:00Z — between the assertion's NotBefore and NotOnOrAfter.
  static constexpr std::int64_t kNow = 1779364800;

  MockMongodbClient        m_db;
  sso::SamlAssertion       m_assertion;
  sso::SamlConditionExpect m_expect;

  void SetUp() override {
    m_assertion.audience        = "xpmile-marvel";
    m_assertion.not_before      = "2026-05-21T11:55:00Z";
    m_assertion.not_on_or_after = "2026-05-21T12:05:00Z";
    m_assertion.recipient = "https://app.test/api/v1/sso/callback/partner";
    m_assertion.in_response_to  = "_req1";

    m_expect.audience  = "xpmile-marvel";
    m_expect.recipient = "https://app.test/api/v1/sso/callback/partner";

    // The login transaction "_req1" answers.
    m_db.getDocumentResult =
        R"({"_id":"_req1","provider":"partner","returnTo":"/main"})";
  }
};

TEST_F(SamlConditionsTest, AudienceMismatch_Rejected) {
  m_assertion.audience = "some-other-sp";
  EXPECT_FALSE(
      sso::validate_saml_conditions(m_assertion, m_expect, m_db, kNow).ok);
}

TEST_F(SamlConditionsTest, ExpiredNotOnOrAfter_Rejected) {
  // An hour past the assertion's NotOnOrAfter.
  EXPECT_FALSE(
      sso::validate_saml_conditions(m_assertion, m_expect, m_db, kNow + 3600)
          .ok);
}

TEST_F(SamlConditionsTest, NotYetValidNotBefore_Rejected) {
  // An hour before the assertion's NotBefore.
  EXPECT_FALSE(
      sso::validate_saml_conditions(m_assertion, m_expect, m_db, kNow - 3600)
          .ok);
}

TEST_F(SamlConditionsTest, RecipientMismatch_Rejected) {
  m_assertion.recipient = "https://evil.test/acs";
  EXPECT_FALSE(
      sso::validate_saml_conditions(m_assertion, m_expect, m_db, kNow).ok);
}

TEST_F(SamlConditionsTest, InResponseTo_ConsumedAtomically) {
  const sso::SamlConditionResult r =
      sso::validate_saml_conditions(m_assertion, m_expect, m_db, kNow);
  ASSERT_TRUE(r.ok) << r.error;
  EXPECT_EQ(r.return_to, "/main");
  EXPECT_EQ(m_db.lastUpdateColl, "sso_transactions");  // claimed single-use
}

TEST_F(SamlConditionsTest, ReplayedInResponseTo_SecondCallRejected) {
  const sso::SamlConditionResult first =
      sso::validate_saml_conditions(m_assertion, m_expect, m_db, kNow);
  ASSERT_TRUE(first.ok) << first.error;
  // Same assertion, same transaction — the guarded claim fails the 2nd time.
  const sso::SamlConditionResult second =
      sso::validate_saml_conditions(m_assertion, m_expect, m_db, kNow);
  EXPECT_FALSE(second.ok);
}

TEST_F(SamlConditionsTest, UnsolicitedResponse_NoInResponseTo_Rejected) {
  m_assertion.in_response_to = "";
  EXPECT_FALSE(
      sso::validate_saml_conditions(m_assertion, m_expect, m_db, kNow).ok);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Phase E.5 — SamlProvider::handle_callback
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

// A complete SAMLResponse — Subject/Conditions/AttributeStatement plus an
// empty enveloped <ds:Signature> template that saml_sign() fills.
const char *kSamlCallbackTemplate = R"SAML(<samlp:Response
  xmlns:samlp="urn:oasis:names:tc:SAML:2.0:protocol"
  xmlns:saml="urn:oasis:names:tc:SAML:2.0:assertion"
  ID="_resp1" Version="2.0">
  <saml:Issuer>https://idp.partner.test/saml</saml:Issuer>
  <saml:Assertion ID="_assert1" Version="2.0"
                  IssueInstant="2026-05-21T12:00:00Z">
    <saml:Issuer>https://idp.partner.test/saml</saml:Issuer>
    <ds:Signature xmlns:ds="http://www.w3.org/2000/09/xmldsig#">
      <ds:SignedInfo>
        <ds:CanonicalizationMethod
          Algorithm="http://www.w3.org/2001/10/xml-exc-c14n#"/>
        <ds:SignatureMethod
          Algorithm="http://www.w3.org/2001/04/xmldsig-more#rsa-sha256"/>
        <ds:Reference URI="#_assert1">
          <ds:Transforms>
            <ds:Transform
              Algorithm="http://www.w3.org/2000/09/xmldsig#enveloped-signature"/>
            <ds:Transform Algorithm="http://www.w3.org/2001/10/xml-exc-c14n#"/>
          </ds:Transforms>
          <ds:DigestMethod Algorithm="http://www.w3.org/2001/04/xmlenc#sha256"/>
          <ds:DigestValue></ds:DigestValue>
        </ds:Reference>
      </ds:SignedInfo>
      <ds:SignatureValue></ds:SignatureValue>
    </ds:Signature>
    <saml:Subject>
      <saml:NameID>jane@partner.test</saml:NameID>
      <saml:SubjectConfirmation Method="urn:oasis:names:tc:SAML:2.0:cm:bearer">
        <saml:SubjectConfirmationData
          Recipient="https://app.test/api/v1/sso/callback/partner"
          NotOnOrAfter="2026-05-21T12:05:00Z"
          InResponseTo="_req1"/>
      </saml:SubjectConfirmation>
    </saml:Subject>
    <saml:Conditions NotBefore="2026-05-21T11:55:00Z"
                     NotOnOrAfter="2026-05-21T12:05:00Z">
      <saml:AudienceRestriction>
        <saml:Audience>xpmile-marvel</saml:Audience>
      </saml:AudienceRestriction>
    </saml:Conditions>
    <saml:AttributeStatement>
      <saml:Attribute Name="email">
        <saml:AttributeValue>jane@partner.test</saml:AttributeValue>
      </saml:Attribute>
      <saml:Attribute Name="displayName">
        <saml:AttributeValue>Jane Partner</saml:AttributeValue>
      </saml:Attribute>
      <saml:Attribute Name="groups">
        <saml:AttributeValue>partner-users</saml:AttributeValue>
        <saml:AttributeValue>xpmile-admins</saml:AttributeValue>
      </saml:Attribute>
    </saml:AttributeStatement>
  </saml:Assertion>
</samlp:Response>)SAML";

} // namespace

class SamlCallbackTest : public ::testing::Test {
protected:
  MockMongodbClient   m_db;
  FakeClock           m_clock;
  sso::ProviderConfig m_config;
  std::string         m_signed_b64;  // base64 of the validly signed response
  std::unique_ptr<sso::SamlProvider> m_provider;

  void SetUp() override {
    sso::EvpPkeyPtr idp_key = generate_rsa();
    ASSERT_TRUE(idp_key);
    const std::string signed_xml =
        saml_sign(kSamlCallbackTemplate, priv_key_pem(idp_key.get()));
    ASSERT_FALSE(signed_xml.empty());
    m_signed_b64 = sso::base64_encode(signed_xml);

    m_config.id               = "partner";
    m_config.protocol         = sso::Protocol::Saml;
    m_config.sp_entity_id     = "xpmile-marvel";
    m_config.idp_signing_cert = make_cert_pem(idp_key.get());

    m_clock.t = 1779364800;  // 2026-05-21T12:00:00Z — inside the window
    m_db.getDocumentResult =
        R"({"_id":"_req1","provider":"partner","returnTo":"/main"})";

    m_provider = std::make_unique<sso::SamlProvider>(
        m_config, "https://app.test", m_db, m_clock);
  }
};

TEST_F(SamlCallbackTest, ValidResponse_ReturnsClaims) {
  const sso::IdentityClaims c =
      m_provider->handle_callback(m_signed_b64, "_req1");
  ASSERT_TRUE(c.ok) << c.error;
  EXPECT_EQ(c.subject, "jane@partner.test");          // NameID
  EXPECT_EQ(c.email, "jane@partner.test");
  EXPECT_TRUE(c.email_verified);
  EXPECT_EQ(c.display_name, "Jane Partner");
  ASSERT_EQ(c.groups.size(), 2u);
  EXPECT_EQ(c.groups[1], "xpmile-admins");
  EXPECT_EQ(c.return_to, "/main");
}

TEST_F(SamlCallbackTest, RegistryRoutesPostCallback_ToSamlProvider) {
  // sso_complete_callback is the protocol-agnostic Phase C path: a SamlProvider
  // plugs in unchanged and the verified claims become a session + cookie.
  sso::SessionManager sm(m_db, m_clock);
  const sso::SsoHttpResult r = sso::sso_complete_callback(
      m_provider.get(), &m_config, m_signed_b64, "_req1", m_db, sm);
  EXPECT_EQ(r.status, 302);
  EXPECT_NE(r.set_cookie.find("xpmile_session="), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Phase F.1 — CSRF double-submit token
// ═══════════════════════════════════════════════════════════════════════════════

TEST(CsrfTest, IssuedToken_MatchesCookieAndHeader_Accepted) {
  EXPECT_TRUE(sso::csrf_ok("POST", "XSRF-TOKEN=abc123", "abc123"));
}

TEST(CsrfTest, MissingHeaderToken_OnMutatingRequest_Rejected) {
  EXPECT_FALSE(sso::csrf_ok("POST", "XSRF-TOKEN=abc123", ""));
}

TEST(CsrfTest, MismatchedToken_Rejected) {
  EXPECT_FALSE(sso::csrf_ok("PUT", "XSRF-TOKEN=abc123", "a-different-token"));
}

TEST(CsrfTest, SafeMethods_GetHead_NotChecked) {
  EXPECT_TRUE(sso::csrf_ok("GET", "", ""));
  EXPECT_TRUE(sso::csrf_ok("HEAD", "", ""));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Phase F.2 — auth-enforcement predicate
// ═══════════════════════════════════════════════════════════════════════════════

TEST(AuthEnforcementTest, ProtectedEndpoint_NoSession_Returns401) {
  // sso_authorize == false → the request must be rejected with 401.
  EXPECT_FALSE(sso::sso_authorize("/api/v1/shipment/shipping", false));
}

TEST(AuthEnforcementTest, ProtectedEndpoint_ValidSession_Proceeds) {
  EXPECT_TRUE(sso::sso_authorize("/api/v1/shipment/shipping", true));
}

TEST(AuthEnforcementTest, ExemptEndpoint_NoSession_StillServed) {
  // Login and the SSO flow run before a session exists.
  EXPECT_TRUE(sso::sso_authorize("/api/v1/account/login", false));
  EXPECT_TRUE(sso::sso_authorize("/api/v1/sso/providers", false));
  EXPECT_TRUE(sso::sso_authorize("/api/v1/sso/callback/okta", false));
}
