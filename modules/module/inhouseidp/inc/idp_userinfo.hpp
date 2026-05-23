#ifndef IDP_USERINFO_HPP
#define IDP_USERINFO_HPP

#include <string>

#include "sso_endpoints.hpp"  // sso::SsoHttpResult
#include "sso_session.hpp"     // sso::IClock

class IMongodbClient;

/**
 * @file idp_userinfo.hpp
 * @brief Transport-agnostic logic for GET /api/v1/idp/userinfo
 *        (docs/design/inhouse-idp/inhouse-idp-design.md §3).
 *
 * Validates the `Authorization: Bearer <access_token>` header by looking
 * up the token in idp.idp_access_tokens (TTL-checked). Returns the
 * user's claims as JSON on success; 401 with a WWW-Authenticate header
 * on missing/invalid/expired.
 *
 * Included for protocol completeness in v1 — the marvel SPA gets claims
 * from the id_token directly and doesn't need to hit /userinfo.
 */
namespace idp {

sso::SsoHttpResult userinfo(const std::string &authorization_header,
                              IMongodbClient &db,
                              sso::IClock &clock);

} // namespace idp

#endif // IDP_USERINFO_HPP
