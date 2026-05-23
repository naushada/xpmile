#include "sso_config.hpp"

#include <exception>

#include "json.hpp"

namespace sso {

namespace {

// Read an optional string field; returns "" when absent or not a string.
std::string str_field(const nlohmann::json &j, const char *key) {
  auto it = j.find(key);
  if (it != j.end() && it->is_string()) return it->get<std::string>();
  return {};
}

// Read an optional array-of-strings field.
std::vector<std::string> str_array(const nlohmann::json &j, const char *key) {
  std::vector<std::string> out;
  auto it = j.find(key);
  if (it != j.end() && it->is_array()) {
    for (const auto &e : *it)
      if (e.is_string()) out.push_back(e.get<std::string>());
  }
  return out;
}

bool parse_provider(const nlohmann::json &p, ProviderConfig &pc,
                    std::string &error) {
  pc.id = str_field(p, "id");
  if (pc.id.empty()) {
    error = "provider is missing required field: id";
    return false;
  }

  const std::string proto = str_field(p, "protocol");
  if (proto == "oidc") {
    pc.protocol = Protocol::Oidc;
  } else if (proto == "saml") {
    pc.protocol = Protocol::Saml;
  } else {
    error = "provider '" + pc.id + "' has missing/unknown protocol: '" +
            proto + "'";
    return false;
  }

  pc.display_name = str_field(p, "displayName");

  // OIDC
  pc.issuer        = str_field(p, "issuer");
  pc.client_id     = str_field(p, "clientId");
  pc.client_secret = str_field(p, "clientSecret");
  pc.scopes        = str_array(p, "scopes");

  // SAML
  pc.idp_entity_id    = str_field(p, "idpEntityId");
  pc.idp_sso_url      = str_field(p, "idpSsoUrl");
  pc.idp_signing_cert = str_field(p, "idpSigningCert");
  pc.sp_entity_id     = str_field(p, "spEntityId");

  // Provisioning
  pc.default_role          = str_field(p, "defaultRole");
  pc.allowed_email_domains = str_array(p, "allowedEmailDomains");

  auto grm = p.find("groupRoleMap");
  if (grm != p.end() && grm->is_object() && !grm->empty()) {
    pc.group_role_map_enabled = true;
    for (auto it = grm->begin(); it != grm->end(); ++it)
      if (it->is_string()) pc.group_role_map[it.key()] = it->get<std::string>();
  } else {
    pc.group_role_map_enabled = false;
  }
  return true;
}

} // namespace

bool parse_sso_config(const std::string &json_text, SsoConfig &out,
                      std::string &error) {
  out = SsoConfig{};

  nlohmann::json j;
  try {
    j = nlohmann::json::parse(json_text);
  } catch (const std::exception &e) {
    error = std::string("malformed JSON: ") + e.what();
    return false;
  }

  if (!j.is_object()) {
    error = "config root must be a JSON object";
    return false;
  }

  const std::string base = str_field(j, "publicBaseUrl");
  if (base.empty()) {
    error = "missing required field: publicBaseUrl";
    return false;
  }
  out.public_base_url = base;

  auto providers = j.find("providers");
  if (providers != j.end()) {
    if (!providers->is_array()) {
      error = "field 'providers' must be an array";
      return false;
    }
    for (const auto &p : *providers) {
      if (!p.is_object()) {
        error = "each entry in 'providers' must be an object";
        return false;
      }
      ProviderConfig pc;
      if (!parse_provider(p, pc, error)) return false;
      out.providers.push_back(std::move(pc));
    }
  }
  return true;
}

} // namespace sso
