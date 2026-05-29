// runcpull::ImageConfig — extract the four fields the bundle writer needs
// out of the OCI image config blob. See image_config.hpp.

#include "image_config.hpp"

#include "json.hpp"

namespace runcpull {

namespace {

// Copy a JSON array-of-strings into a std::vector. Skips non-string
// elements quietly — defensive against producers that leak null /
// number into the array.
void extract_string_array(const nlohmann::json &j,
                          std::vector<std::string> &out) {
  if (!j.is_array()) return;
  for (const auto &elem : j) {
    if (elem.is_string()) {
      out.push_back(elem.get<std::string>());
    }
  }
}

} // namespace

std::optional<ImageConfig> parse_image_config(const std::string &json_body) {
  nlohmann::json doc;
  try {
    doc = nlohmann::json::parse(json_body);
  } catch (const nlohmann::json::exception &) {
    return std::nullopt;
  }
  if (!doc.is_object()) return std::nullopt;

  ImageConfig out;

  // `config` is the runtime config block — { Env, Cmd, Entrypoint, ... }.
  auto cit = doc.find("config");
  if (cit != doc.end() && cit->is_object()) {
    auto eit = cit->find("Env");
    if (eit != cit->end()) extract_string_array(*eit, out.env);

    auto cmdit = cit->find("Cmd");
    if (cmdit != cit->end()) extract_string_array(*cmdit, out.cmd);

    auto entit = cit->find("Entrypoint");
    if (entit != cit->end()) extract_string_array(*entit, out.entrypoint);
  }

  // `rootfs.diff_ids` is the per-uncompressed-layer digest list.
  auto rit = doc.find("rootfs");
  if (rit != doc.end() && rit->is_object()) {
    auto dit = rit->find("diff_ids");
    if (dit != rit->end()) extract_string_array(*dit, out.diff_ids);
  }

  return out;
}

} // namespace runcpull
