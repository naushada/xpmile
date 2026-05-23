#ifndef IDP_WSDB_JWT_SIGNER_HPP
#define IDP_WSDB_JWT_SIGNER_HPP

#include "jwt_signer.hpp"
#include "wsdbproxy.hpp"  // IWsDispatcher

/**
 * @file wsdb_jwt_signer.hpp
 * @brief Production IJwtSigner that delegates the signing op to wsdbagent.
 *
 * Builds a dbproto SIGN_JWT request envelope (op=DbOp::SIGN_JWT, db="idp",
 * coll="idp_signing_keys", sval=kid_hint, doc=to_sign bytes), sends it via
 * the supplied IWsDispatcher (the wsdbagent connection), and parses the
 * response into a SignJwtResult. The private key never appears on the
 * cloud side — only the signature bytes return.
 */
namespace idp {

class WsdbJwtSigner : public IJwtSigner {
public:
  explicit WsdbJwtSigner(IWsDispatcher &dispatcher)
      : m_dispatcher(dispatcher) {}

  SignJwtResult sign(const std::string &kid_hint,
                     const std::string &alg,
                     const std::string &to_sign) override;

private:
  IWsDispatcher &m_dispatcher;
};

} // namespace idp

#endif // IDP_WSDB_JWT_SIGNER_HPP
