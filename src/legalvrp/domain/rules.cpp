#include "legalvrp/domain/rules.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>

#include "legalvrp/domain/paths.hpp"

namespace legalvrp {

ConfigError::ConfigError(std::string source, std::string key, const std::string& reason)
    : std::runtime_error(source + ": " + (key.empty() ? std::string{} : key + ": ") + reason),
      source_(std::move(source)),
      key_(std::move(key)) {}

namespace {

constexpr Minutes kDay = 24 * 60;
constexpr Minutes kWeek = 7 * kDay;
constexpr double kMaxEuros = 1e6;

// Reads one YAML mapping, tracks which keys were consumed, and reports
// every problem with the full dotted key.
class Section {
 public:
  Section(const YAML::Node& node, std::string source, std::string prefix)
      : node_(node), source_(std::move(source)), prefix_(std::move(prefix)) {
    if (!node_.IsMap()) fail("", "expected a mapping");
  }

  [[noreturn]] void fail(const std::string& key, const std::string& reason) const {
    throw ConfigError(source_, full(key), reason);
  }
  [[nodiscard]] std::string full(const std::string& key) const {
    if (prefix_.empty()) return key;
    return key.empty() ? prefix_ : prefix_ + "." + key;
  }

  YAML::Node require(const std::string& key) {
    seen_.insert(key);
    const YAML::Node v = node_[key];
    if (!v) fail(key, "required key is missing");
    return v;
  }

  // Integer minutes in [lo, hi].
  Minutes minutes(const std::string& key, Minutes lo, Minutes hi) {
    const YAML::Node v = require(key);
    long long x = 0;
    bool ok = v.IsScalar();
    if (ok) {
      try {
        x = v.as<long long>();
      } catch (const YAML::Exception&) {
        ok = false;
      }
    }
    if (!ok) fail(key, "expected an integer number of minutes, got '" + scalar(v) + "'");
    if (x < lo || x > hi) {
      fail(key, "must be in [" + std::to_string(lo) + ", " + std::to_string(hi) + "] minutes, got " +
                    std::to_string(x));
    }
    return static_cast<Minutes>(x);
  }

  // Finite euros in [lo, hi].
  Euros euros(const std::string& key, double lo, double hi) {
    const YAML::Node v = require(key);
    double x = 0.0;
    bool ok = v.IsScalar();
    if (ok) {
      try {
        x = v.as<double>();
      } catch (const YAML::Exception&) {
        ok = false;
      }
    }
    if (!ok || !std::isfinite(x)) fail(key, "expected a number (euros), got '" + scalar(v) + "'");
    if (x < lo || x > hi) {
      std::ostringstream os;
      os << "must be in [" << lo << ", " << hi << "] euros, got " << x;
      fail(key, os.str());
    }
    return x;
  }

  void reject_unknown() const {
    for (const auto& kv : node_) {
      const auto k = kv.first.as<std::string>();
      if (!seen_.contains(k)) fail(k, "unknown key (typo?)");
    }
  }

 private:
  static std::string scalar(const YAML::Node& v) {
    return v.IsScalar() ? v.Scalar() : std::string{"<not a scalar>"};
  }

  YAML::Node node_;
  std::string source_;
  std::string prefix_;
  std::set<std::string> seen_;
};

YAML::Node parse_yaml(std::string_view text, const std::string& source) {
  try {
    YAML::Node root = YAML::Load(std::string{text});
    if (!root || root.IsNull()) throw ConfigError(source, "", "file is empty");
    return root;
  } catch (const YAML::ParserException& e) {
    throw ConfigError(source, "", std::string{"invalid YAML: "} + e.what());
  }
}

std::string read_file(const std::filesystem::path& file) {
  std::ifstream in(file, std::ios::binary);
  if (!in) throw ConfigError(file.filename().string(), "", "cannot open " + file.string());
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

}  // namespace

Rules parse_rules(std::string_view yaml, std::string_view source_view) {
  const std::string source{source_view};
  Section s(parse_yaml(yaml, source), source, "");
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
  s.reject_unknown();

  // Cross-key consistency.
  if (r.drive_before_break > r.daily_drive_max) {
    s.fail("drive_before_break", "must be <= daily_drive_max (" + std::to_string(r.daily_drive_max) +
                                     "), got " + std::to_string(r.drive_before_break));
  }
  if (r.work_before_break > r.daily_service_max) {
    s.fail("work_before_break", "must be <= daily_service_max (" +
                                    std::to_string(r.daily_service_max) + "), got " +
                                    std::to_string(r.work_before_break));
  }
  if (r.daily_drive_max > r.weekly_drive_max) {
    s.fail("daily_drive_max", "must be <= weekly_drive_max (" + std::to_string(r.weekly_drive_max) +
                                  "), got " + std::to_string(r.daily_drive_max));
  }
  // "Postpone everything" must stay feasible (MODEL.md): an idle duty is P + R of work.
  if (r.depot_prep + r.depot_close > r.work_before_break) {
    s.fail("depot_prep+depot_close", "must be <= work_before_break (" +
                                         std::to_string(r.work_before_break) + "), got " +
                                         std::to_string(r.depot_prep + r.depot_close));
  }
  return r;
}

Contracts parse_contracts(std::string_view yaml, std::string_view source_view) {
  const std::string source{source_view};
  Section top(parse_yaml(yaml, source), source, "");
  const YAML::Node list = top.require("contracts");
  top.reject_unknown();
  if (!list.IsMap() || list.size() == 0) {
    top.fail("contracts", "expected a non-empty mapping of contract classes");
  }

  Contracts out;
  for (const auto& kv : list) {
    const auto name = kv.first.as<std::string>();
    Section s(kv.second, source, "contracts." + name);
    Contract c;
    c.name = name;
    c.weekly_threshold = s.minutes("weekly_threshold", 0, kWeek);
    c.weekly_service_max = s.minutes("weekly_service_max", 1, kWeek);
    c.cost_per_min_regular = s.euros("cost_per_min_regular", 0.0, kMaxEuros);
    c.cost_per_min_extra = s.euros("cost_per_min_extra", 0.0, kMaxEuros);
    c.fixed_cost_if_used = s.euros("fixed_cost_if_used", 0.0, kMaxEuros);
    s.reject_unknown();
    if (c.weekly_threshold > c.weekly_service_max) {
      s.fail("weekly_threshold", "must be <= weekly_service_max (" +
                                     std::to_string(c.weekly_service_max) + "), got " +
                                     std::to_string(c.weekly_threshold));
    }
    out.push_back(std::move(c));
  }
  std::ranges::sort(out, {}, &Contract::name);
  return out;
}

Costs parse_costs(std::string_view yaml, std::string_view source_view) {
  const std::string source{source_view};
  Section s(parse_yaml(yaml, source), source, "");
  Costs c;
  c.cost_per_km = s.euros("cost_per_km", 0.0, kMaxEuros);
  c.postpone_penalty = s.euros("postpone_penalty", 0.0, kMaxEuros);
  s.reject_unknown();
  // Strictly positive: a free postponement would make "postpone everything" optimal.
  if (c.postpone_penalty <= 0.0) s.fail("postpone_penalty", "must be > 0 euros");
  return c;
}

Rules load_rules(const std::filesystem::path& file) {
  return parse_rules(read_file(file), file.filename().string());
}
Contracts load_contracts(const std::filesystem::path& file) {
  return parse_contracts(read_file(file), file.filename().string());
}
Costs load_costs(const std::filesystem::path& file) {
  return parse_costs(read_file(file), file.filename().string());
}

Config load_config(const std::filesystem::path& dir) {
  return Config{load_rules(dir / "rules.yaml"), load_contracts(dir / "contracts.yaml"),
                load_costs(dir / "costs.yaml")};
}
Config load_config() { return load_config(config_dir()); }

}  // namespace legalvrp
