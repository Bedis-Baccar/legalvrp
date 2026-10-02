#pragma once
// legalvrp::domain — loaders + validation for config/rules.yaml, contracts.yaml, costs.yaml (§3).
//
// Every error is a ConfigError whose message names the source file and the full key,
// e.g.  "contracts.yaml: contracts.part_time.weekly_threshold: must be <= weekly_service_max (1584), got 1600".
// Checks: required keys present, unknown keys rejected (typos), minutes are integers,
// values within range, cross-key consistency.

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

#include "legalvrp/domain/models.hpp"

namespace legalvrp {

class ConfigError : public std::runtime_error {
 public:
  ConfigError(std::string source, std::string key, const std::string& reason);
  [[nodiscard]] const std::string& source() const noexcept { return source_; }
  [[nodiscard]] const std::string& key() const noexcept { return key_; }

 private:
  std::string source_;
  std::string key_;
};

// Parse from YAML text; `source` is used in error messages only.
[[nodiscard]] Rules parse_rules(std::string_view yaml, std::string_view source = "rules.yaml");
[[nodiscard]] Contracts parse_contracts(std::string_view yaml,
                                        std::string_view source = "contracts.yaml");
[[nodiscard]] Costs parse_costs(std::string_view yaml, std::string_view source = "costs.yaml");

// Load from files.
[[nodiscard]] Rules load_rules(const std::filesystem::path& file);
[[nodiscard]] Contracts load_contracts(const std::filesystem::path& file);
[[nodiscard]] Costs load_costs(const std::filesystem::path& file);

struct Config {
  Rules rules;
  Contracts contracts;
  Costs costs;
};

// Loads rules.yaml, contracts.yaml and costs.yaml from `dir` (default: config_dir()).
[[nodiscard]] Config load_config(const std::filesystem::path& dir);
[[nodiscard]] Config load_config();

}  // namespace legalvrp
