// modules/module/inhouseidp/test/sign_jwt_test.cc
//
// Tests for WsdbJwtSigner (Phase A, cloud side). Covers:
//   - The request envelope is built correctly (op, db, coll, sval, doc).
//   - Unsupported alg is rejected before any wire I/O.
//   - Agent error responses propagate via SignJwtResult::error.
//   - Malformed responses are surfaced as errors.
//   - The kid_hint round-trips into sval and back into result.kid.

#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "dbproto.hpp"
#include "sso_util.hpp"
#include "wsdb_jwt_signer.hpp"
#include "wsdbproxy.hpp"  // IWsDispatcher

namespace {

/// IWsDispatcher double — records the bytes sent and replies with a
/// pre-set response. No real socket involved.
class MockDispatcher : public IWsDispatcher {
public:
  std::vector<std::uint8_t> next_response;
  std::vector<std::uint8_t> last_sent;
  bool                      dispatch_called = false;

  std::vector<std::uint8_t>
  dispatch(const std::vector<std::uint8_t> &bson) override {
    dispatch_called = true;
    last_sent       = bson;
    return next_response;
  }
};

std::vector<std::uint8_t>
make_ok_response(std::int32_t reqid,
                 const std::string &kid_used,
                 const std::vector<std::uint8_t> &signature) {
  dbproto::DbResponse r;
  r.reqid = reqid;
  r.ok    = true;
  r.sval  = kid_used;
  r.data  = signature;
  return dbproto::build_response(r);
}

std::vector<std::uint8_t>
make_error_response(std::int32_t reqid, const std::string &msg) {
  dbproto::DbResponse r;
  r.reqid  = reqid;
  r.ok     = false;
  r.errmsg = msg;
  return dbproto::build_response(r);
}

} // namespace

TEST(SignJwt, ConstructsRequestEnvelopeCorrectly) {
  MockDispatcher disp;
  const std::vector<std::uint8_t> fake_sig{0xDE, 0xAD, 0xBE, 0xEF};
  disp.next_response = make_ok_response(0, "k1", fake_sig);

  idp::WsdbJwtSigner signer(disp);
  auto r = signer.sign("current", "RS256", "hdr.payload");

  ASSERT_TRUE(r.ok) << r.error;
  EXPECT_EQ(r.kid, "k1");
  EXPECT_EQ(r.signature,
            sso::base64url_encode(fake_sig.data(), fake_sig.size()));

  // Verify what the proxy *sent* over the wire.
  ASSERT_TRUE(disp.dispatch_called);
  dbproto::DbRequest sent;
  ASSERT_TRUE(dbproto::parse_request(disp.last_sent, sent));
  EXPECT_EQ(sent.op,   DbOp::SIGN_JWT);
  EXPECT_EQ(sent.db,   "idp");
  EXPECT_EQ(sent.coll, "idp_signing_keys");
  EXPECT_EQ(sent.sval, "current");
  EXPECT_EQ(std::string(sent.doc.begin(), sent.doc.end()), "hdr.payload");
}

TEST(SignJwt, RejectsUnsupportedAlg_BeforeAnyWireIO) {
  MockDispatcher disp;
  idp::WsdbJwtSigner signer(disp);

  auto r = signer.sign("current", "HS256", "hdr.payload");

  EXPECT_FALSE(r.ok);
  EXPECT_NE(r.error.find("RS256"), std::string::npos);
  EXPECT_FALSE(disp.dispatch_called)
      << "alg validation must happen before the proxy talks to wsdbagent";
}

TEST(SignJwt, RejectsAlgNone) {
  MockDispatcher disp;
  idp::WsdbJwtSigner signer(disp);
  auto r = signer.sign("current", "none", "hdr.payload");
  EXPECT_FALSE(r.ok);
  EXPECT_FALSE(disp.dispatch_called);
}

TEST(SignJwt, PropagatesErrorFromAgent) {
  MockDispatcher disp;
  disp.next_response = make_error_response(0, "unknown kid");
  idp::WsdbJwtSigner signer(disp);

  auto r = signer.sign("does-not-exist", "RS256", "hdr.payload");

  EXPECT_FALSE(r.ok);
  EXPECT_NE(r.error.find("unknown kid"), std::string::npos);
}

TEST(SignJwt, HandlesMalformedResponse) {
  MockDispatcher disp;
  disp.next_response = {};  // empty — simulates a dispatcher timeout / dead connection
  idp::WsdbJwtSigner signer(disp);

  auto r = signer.sign("current", "RS256", "hdr.payload");

  EXPECT_FALSE(r.ok);
  EXPECT_NE(r.error.find("malformed"), std::string::npos);
}

TEST(SignJwt, SpecificKidIsPassedThrough_AndEchoedBack) {
  MockDispatcher disp;
  const std::vector<std::uint8_t> sig{0x01, 0x02};
  disp.next_response = make_ok_response(0, "kid-2026-05-23-7f3a", sig);

  idp::WsdbJwtSigner signer(disp);
  auto r = signer.sign("kid-2026-05-23-7f3a", "RS256", "h.p");

  ASSERT_TRUE(r.ok);
  EXPECT_EQ(r.kid, "kid-2026-05-23-7f3a");

  dbproto::DbRequest sent;
  ASSERT_TRUE(dbproto::parse_request(disp.last_sent, sent));
  EXPECT_EQ(sent.sval, "kid-2026-05-23-7f3a");
}
