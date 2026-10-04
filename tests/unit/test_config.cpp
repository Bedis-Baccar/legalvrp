// Guards config/rules.yaml against drift from PROJECT_BRIEF §3 (the authoritative table).
// Typed loading and validation with key-naming errors is task T1 (domain/rules).
#include <catch2/catch_test_macros.hpp>
#include <yaml-cpp/yaml.h>

#include <string>
#include <utility>

#include "legalvrp/domain/paths.hpp"
#include "legalvrp/domain/rules.hpp"

namespace {
YAML::Node load(const char* file) {
  return YAML::LoadFile((legalvrp::config_dir() / file).string());
}
}  // namespace

TEST_CASE("rules.yaml holds exactly the brief-3 values (integer minutes)", "[config]") {
  const YAML::Node r = load("rules.yaml");
  const std::pair<const char*, int> expected[] = {
      {"drive_before_break", 270}, {"work_before_break", 360}, {"break_length", 45},
      {"daily_drive_max", 540},    {"daily_service_max", 720}, {"weekly_drive_max", 3360},
      {"daily_rest_min", 660},     {"depot_prep", 20},         {"depot_close", 10},
  };
  for (const auto& [key, value] : expected) {
    INFO("key: " << key);
    REQUIRE(r[key]);
    CHECK(r[key].as<int>() == value);
  }
}

TEST_CASE("every config file parses", "[config]") {
  for (const char* f : {"rules.yaml", "contracts.yaml", "costs.yaml", "solver.yaml", "risk.yaml",
                        "instance_tiny.yaml", "instance_small.yaml", "instance_scale.yaml"}) {
    INFO("file: " << f);
    CHECK_NOTHROW(load(f));
  }
}

TEST_CASE("risk layer: off by default, robust-planning defaults of V1-T7", "[config]") {
  const auto r = legalvrp::load_risk(legalvrp::config_dir() / "risk.yaml");
  CHECK_FALSE(r.enabled);  // plans use the V0 planning rule unless an estimator is given
  CHECK(r.z == 0.5);
  CHECK(r.reserve_minutes == 15);
  CHECK_THROWS(legalvrp::parse_risk("enabled: false\nz: 0.5\nreserve_minutes: 15\nextra: 1\n", "x"));  // strict
}

TEST_CASE("contracts carry the brief-3 weekly thresholds and caps", "[config]") {
  const YAML::Node c = load("contracts.yaml")["contracts"];
  CHECK(c["full_time"]["weekly_threshold"].as<int>() == 2340);
  CHECK(c["full_time"]["weekly_service_max"].as<int>() == 3120);
  CHECK(c["part_time"]["weekly_threshold"].as<int>() == 1440);
  CHECK(c["part_time"]["weekly_service_max"].as<int>() == 1584);
  CHECK(c["temp_agency"]["weekly_threshold"].as<int>() == 2100);
  CHECK(c["temp_agency"]["weekly_service_max"].as<int>() == 3120);
}
