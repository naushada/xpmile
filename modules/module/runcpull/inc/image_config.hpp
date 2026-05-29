#ifndef RUNCPULL_IMAGE_CONFIG_HPP
#define RUNCPULL_IMAGE_CONFIG_HPP

#include <optional>
#include <string>
#include <vector>

/**
 * @file image_config.hpp
 * @brief Parser for the OCI image config blob.
 *
 * Phase C of the runc-pull TDD plan. The image config blob is the JSON
 * document referenced by `<manifest>.config.digest`. We need a handful
 * of fields out of it to emit the stub `config.json` for the runc bundle:
 *
 *  - `config.Env`        — runtime environment (KEY=VALUE strings)
 *  - `config.Cmd`        — default command argv
 *  - `config.Entrypoint` — default entrypoint argv
 *  - `rootfs.diff_ids`   — uncompressed-layer digests (used by v2 cache;
 *                          we parse them for completeness)
 *
 * Anything else (history, OS, architecture, ...) is ignored in v1 — the
 * operator's per-service config.json template overwrites the stub anyway
 * (see operator-runc.md §5).
 */
namespace runcpull {

/**
 * @brief Parsed subset of the OCI image config blob.
 *
 * All four lists default to empty when the corresponding JSON field is
 * absent or null — this matches the OCI spec, which marks every config
 * field as optional.
 */
struct ImageConfig {
  std::vector<std::string> env;         ///< config.Env
  std::vector<std::string> cmd;         ///< config.Cmd
  std::vector<std::string> entrypoint;  ///< config.Entrypoint
  std::vector<std::string> diff_ids;    ///< rootfs.diff_ids ("sha256:<hex>")
};

/**
 * @brief Parse an image config blob from JSON.
 *
 * @return std::nullopt only on malformed top-level JSON (the document
 *         must parse as a JSON object). Missing optional fields are
 *         not errors — they map to empty vectors.
 */
std::optional<ImageConfig> parse_image_config(const std::string &json_body);

} // namespace runcpull

#endif // RUNCPULL_IMAGE_CONFIG_HPP
