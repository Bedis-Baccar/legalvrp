#include "legalvrp/domain/rules.hpp"

#include <algorithm>

#include "legalvrp/domain/detail/yaml_section.hpp"
#include "legalvrp/domain/paths.hpp"

namespace legalvrp {

ConfigError::ConfigError(std::string source, std::string key, const std::string& reason)
    : std::runtime_error(source + ": " + (key.empty() ? std::string{} : key + ": ") + reason),
      source_(std::move(source)),
      key_(std::move(key)) {}

namespace {

using detail::YamlSection;

constexpr Minutes kDay = 24 * 60;
constexpr Minutes kWeek = 7 * kDay;
constexpr double kMaxEuros = 1e6;

std::string must_be_le(const char* other, int other_value, int got) {
  return std::string{"must be <= "} + other + " (" + std::to_string(other_value) + "), got " +
         std::to_string(got);
}

}  // namespace

Rules parse_rules(std::string_view yaml, std::string_view source_view) {
  const std::string source{source_view};
  YamlSection s(detail::parse_yaml(std::string{yaml}, source), source, "");
  Rules r;
  r.drive_before_break = s.minutes("drive_before_break", 1, kDay);
  r.work_before_break = s.minutes("work_before_break", 1, kDay);
  r.break_length = s.minutes("break_length", 1, kDay);
  r.daily_drive_max = s.minutes("daily_drive_max", 1, kDay);
  r.daily_service_max = s.minutes("daily_service_max", 1, kDay);
  r.weekly_drive_max = s.minutes("weekly_drive_max", 1, kWeek);
  r.daily_rest_min = s.minutes("daily_rest_min", 0, kDay - 1);
  r.depot_prep = s.minutes("depot_prep", 0, kDay);
  r.depot_close = s.minutes("depot_close", 0, kDay);
  // No work before 05:00 and none in the 21:00-06:00 night period beyond its first hour:
  // the night-work rules (L3312-1) then cannot apply (D-020).
  r.earliest_duty_start = s.minutes("earliest_duty_start", 300, kDay);
  r.latest_duty_end = s.minutes("latest_duty_end", 0, 21 * 60);
  s.reject_unknown();

  // Cross-key consistency.
  if (r.drive_before_break > r.daily_drive_max) {
    s.fail("drive_before_break", must_be_le("daily_drive_max", r.daily_drive_max, r.drive_before_break));
  }
  if (r.work_before_break > r.daily_service_max) {
    s.fail("work_before_break",
           must_be_le("daily_service_max", r.daily_service_max, r.work_before_break));
  }
  if (r.daily_drive_max > r.weekly_drive_max) {
    s.fail("daily_drive_max", must_be_le("weekly_drive_max", r.weekly_drive_max, r.daily_drive_max));
  }
  // "Postpone everything" must stay feasible (MODEL.md): an idle duty is P + R of work.
  if (r.depot_prep + r.depot_close > r.work_before_break) {
    s.fail("depot_prep+depot_close",
           must_be_le("work_before_break", r.work_before_break, r.depot_prep + r.depot_close));
  }
  if (r.earliest_duty_start >= r.latest_duty_end) {
    s.fail("latest_duty_end", "must be after earliest_duty_start (" +
                                  std::to_string(r.earliest_duty_start) + ")");
  }
  return r;
}

Contracts parse_contracts(std::string_view yaml, std::string_view source_view) {
  const std::string source{source_view};
  YamlSection top(detail::parse_yaml(std::string{yaml}, source), source, "");
  auto classes = top.named_sections("contracts");
  top.reject_unknown();

  Contracts out;
  for (auto& [name, s] : classes) {
    Contract c;
    c.name = name;
    c.weekly_threshold = s.minutes("weekly_threshold", 0, kWeek);
    c.weekly_service_max = s.minutes("weekly_service_max", 1, kWeek);
    c.cost_per_min_regular = s.euros("cost_per_min_regular", 0.0, kMaxEuros);
    c.cost_per_min_extra = s.euros("cost_per_min_extra", 0.0, kMaxEuros);
    c.fixed_cost_if_used = s.euros("fixed_cost_if_used", 0.0, kMaxEuros);
    s.reject_unknown();
    if (c.weekly_threshold > c.weekly_service_max) {
      s.fail("weekly_threshold",
             must_be_le("weekly_service_max", c.weekly_service_max, c.weekly_threshold));
    }
    out.push_back(std::move(c));
  }
  std::ranges::sort(out, {}, &Contract::name);
  return out;
}

Costs parse_costs(std::string_view yaml, std::string_view source_view) {
  const std::string source{source_view};
  YamlSection s(detail::parse_yaml(std::string{yaml}, source), source, "");
  Costs c;
  c.cost_per_km = s.euros("cost_per_km", 0.0, kMaxEuros);
  c.postpone_penalty = s.euros("postpone_penalty", 0.0, kMaxEuros);
  c.postpone_escalation = s.real("postpone_escalation", 1.0, 100.0);
  c.unserved_end_penalty = s.euros("unserved_end_penalty", 0.0, kMaxEuros);
  s.reject_unknown();
  // Strictly positive: a free postponement would make "postpone everything" optimal.
  if (c.postpone_penalty <= 0.0) s.fail("postpone_penalty", "must be > 0 euros");
  if (c.unserved_end_penalty < c.postpone_penalty) {
    s.fail("unserved_end_penalty", "must be >= postpone_penalty");
  }
  return c;
}

Rules load_rules(const std::filesystem::path& file) {
  const auto src = file.filename().string();
  return parse_rules(detail::read_text_file(file, src), src);
}
Contracts load_contracts(const std::filesystem::path& file) {
  const auto src = file.filename().string();
  return parse_contracts(detail::read_text_file(file, src), src);
}
Costs load_costs(const std::filesystem::path& file) {
  const auto src = file.filename().string();
  return parse_costs(detail::read_text_file(file, src), src);
}

Config load_config(const std::filesystem::path& dir) {
  return Config{load_rules(dir / "rules.yaml"), load_contracts(dir / "contracts.yaml"),
                load_costs(dir / "costs.yaml")};
}
Config load_config() { return load_config(config_dir()); }

}  // namespace legalvrp
