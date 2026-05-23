#ifndef IDP_PBKDF2_CREDENTIALS_HPP
#define IDP_PBKDF2_CREDENTIALS_HPP

#include <string>

#include "idp_password.hpp"
#include "idp_password_hasher.hpp"
#include "mongodbc.hpp"   // MongodbClient::{hash_password, verify_password}

/**
 * @file idp_pbkdf2_credentials.hpp
 * @brief Production password verifier / hasher impls — thin wrappers
 *        around the existing PBKDF2-SHA256 routines in
 *        `MongodbClient::{verify_password,hash_password}` (mongodbc.cpp).
 *
 * The hash format produced/checked is exactly the existing project's:
 *     $pbkdf2-sha256$i=<iters>$<salt-b64>$<hash-b64>
 *
 * Header-only on purpose — both methods forward in one line to a
 * static helper. Keeping the impls inline lets the IdP routes use them
 * without dragging in a new translation unit, and the static methods
 * being on `MongodbClient` (not `IMongodbClient`) is fine here because
 * the verifier never touches the DB itself; it's purely a CPU op.
 */
namespace idp {

class PbkdfPasswordVerifier : public IPasswordVerifier {
public:
  bool verify(const std::string &password,
               const std::string &hash) override {
    return MongodbClient::verify_password(password, hash);
  }
};

class PbkdfPasswordHasher : public IPasswordHasher {
public:
  std::string hash(const std::string &password) override {
    return MongodbClient::hash_password(password);
  }
};

} // namespace idp

#endif // IDP_PBKDF2_CREDENTIALS_HPP
