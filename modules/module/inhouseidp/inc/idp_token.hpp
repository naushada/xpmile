#ifndef IDP_TOKEN_HPP
#define IDP_TOKEN_HPP

#include <map>
#include <string>

#include "sso_endpoints.hpp"  // sso::SsoHttpResult
#include "sso_session.hpp"     // sso::IClock

class IMongodbClient;

/**
 * @file idp_token.hpp
 * @brief Transport-agnostic logic for POST /api/v1/idp/token
 *        (docs/design/inhouse-idp/inhouse-idp-design.md §3).
 *
 * Atomically claims the code (idp.idp_codes — guarded `update_collection`
 * with `consumed: {$exists: false}` filter), validates PKCE, validates
 * client_id + redirect_uri match the code's, signs the id_token via the
 * supplied IJwtSigner (production: WsdbJwtSigner → wsdbagent SIGN_JWT;
 * tests: MockJwtSigner or RealRs256Signer), mints an opaque access
 * token, and returns `{access_token, token_type:"Bearer", expires_in,
 * id_token, scope}`.
 *
 * Negative paths return 4xx with an OAuth2-style {error, error_description}.
 */
namespace idp {

class IJwtSigner;

sso::SsoHttpResult token(
    const std::map<std::string, std::string> &form,
    IMongodbClient &db,
    IJwtSigner &signer,
    const std::string &issuer,
    sso::IClock &clock);

} // namespace idp

#endif // IDP_TOKEN_HPP
