// Architecture guard (PROJECT_BRIEF §0.4, §8, task T4 acceptance):
// the checker must never include model/, heuristics/ or week/.
// CMake already prevents linking; this catches header-only leaks.
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "legalvrp/domain/paths.hpp"

namespace fs = std::filesystem;

namespace {
std::vector<std::string> forbidden_includes(const fs::path& dir,
                                            const std::vector<std::string>& banned) {
  std::vector<std::string> hits;
  for (const auto& entry : fs::recursive_directory_iterator(dir)) {
    if (!entry.is_regular_file()) continue;
    std::ifstream in(entry.path());
    std::string line;
    while (std::getline(in, line)) {
      if (line.find("#include") == std::string::npos) continue;
      for (const auto& b : banned) {
        if (line.find("legalvrp/" + b + "/") != std::string::npos) {
          hits.push_back(entry.path().filename().string() + ": " + line);
        }
      }
    }
  }
  return hits;
}
const fs::path kSrc = legalvrp::repo_root() / "src" / "legalvrp";
}  // namespace

TEST_CASE("checker depends on domain only", "[layering]") {
  const auto hits = forbidden_includes(kSrc / "check",
                                       {"model", "heuristics", "week", "data", "kpi", "estimate"});
  INFO([&] { std::string s; for (const auto& h : hits) s += h + "\n"; return s; }());
  CHECK(hits.empty());
}

TEST_CASE("domain depends on nothing else", "[layering]") {
  const auto hits = forbidden_includes(
      kSrc / "domain", {"model", "heuristics", "week", "data", "kpi", "estimate", "check"});
  CHECK(hits.empty());
}

TEST_CASE("model does not reach into heuristics or week", "[layering]") {
  const auto hits = forbidden_includes(kSrc / "model", {"heuristics", "week", "check"});
  CHECK(hits.empty());
}
