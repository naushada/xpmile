#include "sso_registry.hpp"

#include <utility>

#include <openssl/sha.h>

namespace sso {

bool ProviderRegistry::reload_if_changed(const std::string &raw_json) {
  unsigned char digest[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const unsigned char *>(raw_json.data()),
         raw_json.size(), digest);
  std::string h(reinterpret_cast<const char *>(digest), SHA256_DIGEST_LENGTH);

  if (h == m_hash) return false;  // unchanged since the last call
  m_hash = std::move(h);          // remember this document, good or bad

  SsoConfig   parsed;
  std::string error;
  if (!parse_sso_config(raw_json, parsed, error)) {
    // Invalid — keep the last-good config so login is never taken down.
    return false;
  }
  m_config = std::move(parsed);
  return true;
}

const ProviderConfig *ProviderRegistry::find(
    const std::string &provider_id) const {
  for (const auto &p : m_config.providers)
    if (p.id == provider_id) return &p;
  return nullptr;
}

} // namespace sso
