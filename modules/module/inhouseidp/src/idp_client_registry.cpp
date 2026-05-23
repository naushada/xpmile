#include "idp_client_registry.hpp"

#include "json.hpp"
#include "mongodbc.hpp"

namespace idp {

namespace {

std::vector<std::string> to_string_array(const nlohmann::json &arr) {
  std::vector<std::string> out;
  if (!arr.is_array()) return out;
  out.reserve(arr.size());
  for (const auto &e : arr) {
    if (e.is_string()) out.push_back(e.get<std::string>());
  }
  return out;
}

} // namespace

std::int32_t IdpClientRegistry::reload(IMongodbClient &db) {
  m_clients.clear();

  const std::string raw =
      db.get_documents(kColl, /*query=*/std::string{},
                          /*projection=*/std::string{});
  if (raw.empty()) return 0;

  nlohmann::json arr;
  try {
    arr = nlohmann::json::parse(raw);
  } catch (const std::exception &) {
    return 0;
  }
  if (!arr.is_array()) return 0;

  for (const auto &row : arr) {
    if (!row.is_object()) continue;
    IdpClient c;
    // Allow either {"_id":"..."} (Mongo doc id) or {"clientId":"..."}.
    c.clientId         = row.value("_id",      std::string{});
    if (c.clientId.empty()) {
      c.clientId       = row.value("clientId", std::string{});
    }
    c.clientName       = row.value("clientName",       std::string{});
    c.clientSecretHash = row.value("clientSecretHash", std::string{});
    c.redirectUris             = to_string_array(row.value("redirectUris",
                                                              nlohmann::json::array()));
    c.postLogoutRedirectUris   = to_string_array(row.value("postLogoutRedirectUris",
                                                              nlohmann::json::array()));
    c.scopes                   = to_string_array(row.value("scopes",
                                                              nlohmann::json::array()));
    c.grantTypes               = to_string_array(row.value("grantTypes",
                                                              nlohmann::json::array()));
    if (!c.clientId.empty()) m_clients.push_back(std::move(c));
  }
  return static_cast<std::int32_t>(m_clients.size());
}

const IdpClient *IdpClientRegistry::find(const std::string &client_id) const {
  for (const auto &c : m_clients) {
    if (c.clientId == client_id) return &c;
  }
  return nullptr;
}

bool IdpClientRegistry::validate_redirect_uri(
    const std::string &client_id, const std::string &redirect_uri) const {
  const IdpClient *c = find(client_id);
  if (!c || redirect_uri.empty()) return false;
  for (const auto &registered : c->redirectUris) {
    if (registered == redirect_uri) return true;  // exact byte match
  }
  return false;
}

bool IdpClientRegistry::validate_post_logout_redirect_uri(
    const std::string &client_id, const std::string &uri) const {
  const IdpClient *c = find(client_id);
  if (!c || uri.empty()) return false;
  for (const auto &registered : c->postLogoutRedirectUris) {
    if (registered == uri) return true;
  }
  return false;
}

} // namespace idp
