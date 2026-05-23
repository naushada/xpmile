#include "wsdb_jwt_signer.hpp"

#include <cstdint>
#include <vector>

#include "dbproto.hpp"
#include "sso_util.hpp"  // sso::base64url_encode

namespace idp {

SignJwtResult WsdbJwtSigner::sign(const std::string &kid_hint,
                                   const std::string &alg,
                                   const std::string &to_sign) {
  SignJwtResult result;

  // RS256 is the only accepted alg — match the verifier in sso_jwt.cpp.
  // Other algs (none, HS*, ES*) are rejected before any wire I/O so an
  // attacker can't trigger a wsdbagent round-trip with an invalid alg.
  if (alg != "RS256") {
    result.ok    = false;
    result.error = "unsupported alg: " + alg + " (only RS256)";
    return result;
  }

  // Build the SIGN_JWT envelope. The on-prem dispatcher reads the active
  // signing key from idp.idp_signing_keys, signs `doc`, and returns the
  // raw signature bytes in `data` and the resolved kid in `sval`.
  const std::vector<std::uint8_t> to_sign_bytes(to_sign.begin(), to_sign.end());
  auto [reqid, req_bytes] = dbproto::build_request(
      DbOp::SIGN_JWT,
      /*db=*/   "idp",
      /*coll=*/ "idp_signing_keys",
      /*doc=*/  to_sign_bytes,
      /*doc2=*/ {},
      /*sval=*/ kid_hint);
  (void)reqid;  // matched internally by the dispatcher's per-request map

  // Blocks until the agent replies or the dispatcher times out.
  const auto resp_bytes = m_dispatcher.dispatch(req_bytes);

  dbproto::DbResponse resp;
  if (!dbproto::parse_response(resp_bytes, resp)) {
    result.ok    = false;
    result.error = "malformed SIGN_JWT response from wsdbagent";
    return result;
  }

  if (!resp.ok) {
    result.ok    = false;
    result.error = resp.errmsg.empty()
                       ? "SIGN_JWT failed (no error message)"
                       : resp.errmsg;
    return result;
  }

  // Encode the raw signature bytes per RFC 7515 §2 (base64url, no padding).
  // resp.data is the raw RSA-PKCS#1 v1.5 signature from the on-prem side.
  result.ok        = true;
  result.kid       = resp.sval;
  result.signature = sso::base64url_encode(resp.data.data(), resp.data.size());
  return result;
}

} // namespace idp
