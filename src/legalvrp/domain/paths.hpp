#pragma once
// legalvrp::domain — repository paths and build info.

#include <filesystem>
#include <string_view>

namespace legalvrp {

inline constexpr std::string_view kVersion = LEGALVRP_VERSION;

// Repository root: $LEGALVRP_ROOT if set, otherwise the source tree the binary was built from.
[[nodiscard]] std::filesystem::path repo_root();

[[nodiscard]] inline std::filesystem::path config_dir() { return repo_root() / "config"; }
[[nodiscard]] inline std::filesystem::path fixtures_dir() { return repo_root() / "tests" / "fixtures"; }
[[nodiscard]] inline std::filesystem::path results_dir() { return repo_root() / "results"; }

}  // namespace legalvrp
