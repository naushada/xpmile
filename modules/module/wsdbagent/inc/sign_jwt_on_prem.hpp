#ifndef SIGN_JWT_ON_PREM_HPP
#define SIGN_JWT_ON_PREM_HPP

#include "dbproto.hpp"

class IMongodbClient;

/**
 * @file sign_jwt_on_prem.hpp
 * @brief Handle the SIGN_JWT op on the wsdbagent side.
 *
 * The signing private key never leaves the on-prem MongoDB. This handler
 * loads the active signing key from idp.idp_signing_keys, parses the
 * PEM with OpenSSL, signs the request's `doc` bytes with RS256 (SHA-256
 * + RSA-PKCS#1 v1.5), and writes the result into `rsp.data` + `rsp.sval`
 * (the resolved kid).
 *
 * On failure: rsp.ok=false and rsp.errmsg populated. The dispatcher is
 * expected to have already populated rsp.reqid from req.reqid.
 *
 * See docs/design/inhouse-idp/inhouse-idp-design.md §2.
 */
namespace agent {

void sign_jwt_on_prem(IMongodbClient &db,
                      const dbproto::DbRequest &req,
                      dbproto::DbResponse &rsp);

} // namespace agent

#endif // SIGN_JWT_ON_PREM_HPP
