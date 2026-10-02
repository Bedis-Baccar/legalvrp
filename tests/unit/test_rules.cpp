// T1 acceptance: invalid configs fail with a message naming the key.
#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <string>

#include "legalvrp/domain/paths.hpp"
#include "legalvrp/domain/rules.hpp"

using namespace legalvrp;

namespace {

const std::string kRules = R"(
drive_before_break: 270
work_before_break: 360
break_length: 45
daily_drive_max: 540
daily_service_max: 720
weekly_drive_max: 3360
daily_rest_min: 660
depot_prep: 20
depot_close: 10
)";

const std::string kContracts = R"(
contracts:
  full_time:
    weekly_threshold: 2340
    weekly_service_max: 3120
    cost_per_min_regular: 0.01
    cost_per_min_extra: 0.45
    fixed_cost_if_used: 0.0
  part_time:
    weekly_threshold: 1440
    weekly_service_max: 1584
    cost_per_min_regular: 0.01
    cost_per_min_extra: 0.40
    fixed_cost_if_used: 0.0
)";

const std::string kCosts = "cost_per_km: 1.60\npostpone_penalty: 200.0\n";

std::string replace(std::string text, const std::string& from, const std::string& to) {
  const auto pos = text.find(from);
  REQUIRE(pos != std::string::npos);
  return text.replace(pos, from.size(), to);
}

// Runs `f`, expects a ConfigError, returns it so the test can inspect key and message.
ConfigError expect_error(const std::function<void()>& f) {
  try {
    f();
  } catch (const ConfigError& e) {
    return e;
  }
  FAIL("expected a ConfigError");
  throw;  // unreachable
}

void check_names_key(const ConfigError& e, const std::string& key) {
  INFO("message: " << e.what());
  CHECK(e.key() == key);
  CHECK(std::string{e.what()}.find(key) != std::string::npos);
}

}  // namespace

// ---------------------------------------------------------------- valid input

TEST_CASE("repository config loads with the brief-3 values", "[rules]") {
  const Config c = load_config();
  CHECK(c.rules.drive_before_break == 270);
  CHECK(c.rules.work_before_break == 360);
  CHECK(c.rules.break_length == 45);
  CHECK(c.rules.daily_drive_max == 540);
  CHECK(c.rules.daily_service_max == 720);
  CHECK(c.rules.weekly_drive_max == 3360);
  CHECK(c.rules.daily_rest_min == 660);
  CHECK(c.rules.depot_prep == 20);
  CHECK(c.rules.depot_close == 10);

  REQUIRE(c.contracts.size() == 3);  // sorted by name
  CHECK(c.contracts[0].name == "full_time");
  CHECK(c.contracts[1].name == "part_time");
  CHECK(c.contracts[2].name == "temp_agency");
  CHECK(c.contracts[1].weekly_service_max == 1584);
  CHECK(c.costs.postpone_penalty > 0.0);
}

// ---------------------------------------------------------------- rules.yaml

TEST_CASE("rules: missing key is named", "[rules]") {
  const auto e = expect_error([] { (void)parse_rules(replace(kRules, "break_length: 45\n", "")); });
  check_names_key(e, "break_length");
}

TEST_CASE("rules: non-integer minutes are rejected", "[rules]") {
  const auto e = expect_error(
      [] { (void)parse_rules(replace(kRules, "drive_before_break: 270", "drive_before_break: 270.5")); });
  check_names_key(e, "drive_before_break");
}

TEST_CASE("rules: text instead of minutes is rejected", "[rules]") {
  const auto e = expect_error(
      [] { (void)parse_rules(replace(kRules, "break_length: 45", "break_length: 45min")); });
  check_names_key(e, "break_length");
}

TEST_CASE("rules: out-of-range value is rejected", "[rules]") {
  const auto e = expect_error(
      [] { (void)parse_rules(replace(kRules, "daily_drive_max: 540", "daily_drive_max: -5")); });
  check_names_key(e, "daily_drive_max");
}

TEST_CASE("rules: unknown key (typo) is rejected", "[rules]") {
  const auto e = expect_error([] { (void)parse_rules(kRules + "brake_length: 45\n"); });
  check_names_key(e, "brake_length");
}

TEST_CASE("rules: drive_before_break must not exceed daily_drive_max", "[rules]") {
  const auto e = expect_error(
      [] { (void)parse_rules(replace(kRules, "drive_before_break: 270", "drive_before_break: 600")); });
  check_names_key(e, "drive_before_break");
}

TEST_CASE("rules: prep + close must fit in one work segment", "[rules]") {
  const auto e =
      expect_error([] { (void)parse_rules(replace(kRules, "depot_prep: 20", "depot_prep: 355")); });
  check_names_key(e, "depot_prep+depot_close");
}

TEST_CASE("rules: invalid YAML and empty file are reported with the source", "[rules]") {
  const auto e1 = expect_error([] { (void)parse_rules("a: [1, 2", "rules.yaml"); });
  CHECK(std::string{e1.what()}.starts_with("rules.yaml: "));
  const auto e2 = expect_error([] { (void)parse_rules("", "rules.yaml"); });
  CHECK(std::string{e2.what()}.find("empty") != std::string::npos);
}

// ---------------------------------------------------------------- contracts.yaml

TEST_CASE("contracts: missing field is named with its class", "[rules]") {
  const auto e = expect_error([] {
    (void)parse_contracts(replace(kContracts, "    cost_per_min_extra: 0.40\n", ""));
  });
  check_names_key(e, "contracts.part_time.cost_per_min_extra");
}

TEST_CASE("contracts: threshold above the weekly cap is rejected", "[rules]") {
  const auto e = expect_error([] {
    (void)parse_contracts(replace(kContracts, "weekly_threshold: 1440", "weekly_threshold: 1600"));
  });
  check_names_key(e, "contracts.part_time.weekly_threshold");
}

TEST_CASE("contracts: negative cost is rejected", "[rules]") {
  const auto e = expect_error([] {
    (void)parse_contracts(replace(kContracts, "cost_per_min_extra: 0.45", "cost_per_min_extra: -1"));
  });
  check_names_key(e, "contracts.full_time.cost_per_min_extra");
}

TEST_CASE("contracts: unknown field and missing section are rejected", "[rules]") {
  const auto e1 = expect_error(
      [] { (void)parse_contracts(replace(kContracts, "    fixed_cost_if_used: 0.0\n  part_time:",
                                         "    fixed_cost_if_used: 0.0\n    bonus: 5\n  part_time:")); });
  check_names_key(e1, "contracts.full_time.bonus");
  const auto e2 = expect_error([] { (void)parse_contracts("other: 1\n"); });
  check_names_key(e2, "contracts");
}

// ---------------------------------------------------------------- costs.yaml

TEST_CASE("costs: postponement must have a positive penalty", "[rules]") {
  const auto e = expect_error(
      [] { (void)parse_costs(replace(kCosts, "postpone_penalty: 200.0", "postpone_penalty: 0")); });
  check_names_key(e, "postpone_penalty");
}

TEST_CASE("costs: non-numeric value is rejected", "[rules]") {
  const auto e = expect_error(
      [] { (void)parse_costs(replace(kCosts, "cost_per_km: 1.60", "cost_per_km: cheap")); });
  check_names_key(e, "cost_per_km");
}

TEST_CASE("missing config file names the file", "[rules]") {
  const auto e = expect_error([] { (void)load_rules(config_dir() / "does_not_exist.yaml"); });
  CHECK(std::string{e.what()}.find("does_not_exist.yaml") != std::string::npos);
}
