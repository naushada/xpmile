# inhouseidp — In-house OIDC identity provider

C++ module that implements the *issuer* side of OIDC + an SMTP-backed password-reset flow. Every type lives in the **`idp::`** namespace.

For the protocol-level design, see [`docs/design/inhouse-idp/inhouse-idp-design.md`](../../../docs/design/inhouse-idp/inhouse-idp-design.md). For the operator-facing deploy guide, see [`docs/inhouse-idp.md`](../../../docs/inhouse-idp.md). The longer reference (databases, dual-DB layout, all 8 routes, etc.) is in [`codebase.md`](../../../codebase.md) → *inhouseidp module*.

---

## Layout

```
inhouseidp/
├── inc/
│   ├── jwt_signer.hpp              IJwtSigner interface + SignJwtResult
│   ├── wsdb_jwt_signer.hpp         WsdbJwtSigner — production impl over the wsdbagent SIGN_JWT op
│   ├── idp_session.hpp             IdpSessionManager — create/lookup/revoke over idp.sessions
│   ├── idp_cookie.hpp              xpmile_idp_session + xpmile_idp_pending cookie helpers
│   ├── idp_client_registry.hpp     IdpClientRegistry — RPs loaded from idp.idp_clients
│   ├── idp_password.hpp            IPasswordVerifier interface
│   ├── idp_password_hasher.hpp     IPasswordHasher interface
│   ├── idp_pbkdf2_credentials.hpp  PbkdfPasswordVerifier + PbkdfPasswordHasher — wrap MongodbClient::{verify,hash}_password
│   ├── idp_email_sender.hpp        IEmailSender interface
│   ├── idp_smtp_sender.hpp         SmtpEmailSender — wraps SMTP::Account + SMTP::User
│   ├── idp_discovery.hpp           /.well-known/openid-configuration builder + handler
│   ├── idp_jwks.hpp                /api/v1/idp/jwks builder + handler (RSA modulus + exponent extraction)
│   ├── idp_authorize.hpp           /api/v1/idp/authorize logic
│   ├── idp_login.hpp               /api/v1/idp/login logic
│   ├── idp_token.hpp               /api/v1/idp/token logic (atomic code claim + sign)
│   ├── idp_userinfo.hpp            /api/v1/idp/userinfo logic
│   ├── idp_end_session.hpp         /api/v1/idp/end_session logic
│   └── idp_password_reset.hpp      /api/v1/idp/password/{reset_request,reset_confirm} logic
├── src/                            matching .cpp impls
└── test/                           118 GTest under Idp* / Jwks / PasswordReset* / SignJwt* prefixes
```

## Routing

`/api/v1/idp/*` and `/.well-known/openid-configuration` dispatch through `MicroService::handle_idp()` in `webservice.cpp` — same shape as `handle_sso()`: parse, call the transport-agnostic handler, render the `SsoHttpResult` onto the wire. The `IDP_ISSUER` env var is read on every request — when unset the routes return 503 (marvel dyno posture); when set they activate (idp dyno posture).

## Test posture

All 118 unit tests run inside the standard `offtarget` GTest binary against mocks behind `IMongodbClient` / `IJwtSigner` / `IEmailSender` / `IPasswordVerifier` / `IPasswordHasher`. No live MongoDB, no network. A build-time RSA-2048 keypair fixture (`test/CMakeLists.txt` → `idp_test_keys` target) drives the end-to-end `IdpToken.EndToEnd_RealRsaSign_VerifyWithJwks` test that signs a real id_token and verifies it via `sso::verify_jwt`.

## Adding a new endpoint

Same recipe as the SSO module:

1. Add the transport-agnostic logic as a free function in a new `idp_<name>.hpp` / `.cpp` pair. Take an `IMongodbClient&` + any other interface dependencies; return a `sso::SsoHttpResult` (reused from v1.0 — the contract is identical).
2. Add a unit test in `test/<name>_test.cc` with mock impls.
3. Wire the route in `MicroService::handle_idp()` (`modules/module/webservice/src/webservice.cpp`).
4. Update [`docs/design/inhouse-idp/inhouse-idp-design.md`](../../../docs/design/inhouse-idp/inhouse-idp-design.md) §3 and the *Wire adapter* table in [`codebase.md`](../../../codebase.md).
