#include <catch2/catch_test_macros.hpp>

#include <filesystem>

#include "legalvrp/domain/paths.hpp"

TEST_CASE("build info and repository layout are available", "[smoke]") {
  CHECK_FALSE(legalvrp::kVersion.empty());
  CHECK(std::filesystem::exists(legalvrp::repo_root() / "docs" / "PROJECT_BRIEF.md"));
  CHECK(std::filesystem::is_directory(legalvrp::config_dir()));
  CHECK(std::filesystem::is_directory(legalvrp::fixtures_dir()));
}
