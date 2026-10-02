#include "legalvrp/domain/detail/yaml_section.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "legalvrp/domain/rules.hpp"

namespace legalvrp::detail {

namespace {
std::string scalar_text(const YAML::Node& v) {
  return v.IsScalar() ? v.Scalar() : std::string{"<not a scalar>"};
}
std::string num(double x) {
  std::ostringstream os;
  os << x;
  return os.str();
}
}  // namespace

YamlSection::YamlSection(const YAML::Node& node, std::string source, std::string prefix)
    : node_(node), source_(std::move(source)), prefix_(std::move(prefix)) {
  if (!node_.IsMap()) fail("", "expected a mapping");
}

void YamlSection::fail(const std::string& key, const std::string& reason) const {
  throw ConfigError(source_, full(key), reason);
}

std::string YamlSection::full(const std::string& key) const {
  if (prefix_.empty()) return key;
  if (key.empty()) return prefix_;
  return prefix_ + "." + key;
}

YAML::Node YamlSection::require(const std::string& key) {
  seen_.insert(key);
  YAML::Node v = node_[key];
  if (!v) fail(key, "required key is missing");
  return v;
}

YamlSection YamlSection::child(const std::string& key) {
  return YamlSection(require(key), source_, full(key));
}

long long YamlSection::integer(const std::string& key, long long lo, long long hi) {
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
  if (!ok) fail(key, "expected an integer, got '" + scalar_text(v) + "'");
  if (x < lo || x > hi) {
    fail(key, "must be in [" + std::to_string(lo) + ", " + std::to_string(hi) + "], got " +
                  std::to_string(x));
  }
  return x;
}

Minutes YamlSection::minutes(const std::string& key, Minutes lo, Minutes hi) {
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
  if (!ok) fail(key, "expected an integer number of minutes, got '" + scalar_text(v) + "'");
  if (x < lo || x > hi) {
    fail(key, "must be in [" + std::to_string(lo) + ", " + std::to_string(hi) + "] minutes, got " +
                  std::to_string(x));
  }
  return static_cast<Minutes>(x);
}

double YamlSection::real(const std::string& key, double lo, double hi) {
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
  if (!ok || !std::isfinite(x)) fail(key, "expected a number, got '" + scalar_text(v) + "'");
  if (x < lo || x > hi) fail(key, "must be in [" + num(lo) + ", " + num(hi) + "], got " + num(x));
  return x;
}

bool YamlSection::boolean(const std::string& key) {
  const YAML::Node v = require(key);
  try {
    return v.as<bool>();
  } catch (const YAML::Exception&) {
    fail(key, "expected true or false, got '" + scalar_text(v) + "'");
  }
}

std::string YamlSection::text(const std::string& key) {
  const YAML::Node v = require(key);
  if (!v.IsScalar() || v.Scalar().empty()) fail(key, "expected a non-empty string");
  return v.Scalar();
}

std::pair<long long, long long> YamlSection::integer_range(const std::string& key, long long min,
                                                           long long max) {
  const YAML::Node v = require(key);
  if (!v.IsSequence() || v.size() != 2) fail(key, "expected [lo, hi]");
  long long lo = 0;
  long long hi = 0;
  try {
    lo = v[0].as<long long>();
    hi = v[1].as<long long>();
  } catch (const YAML::Exception&) {
    fail(key, "expected two integers [lo, hi]");
  }
  if (lo > hi) fail(key, "lo must be <= hi");
  if (lo < min || hi > max) {
    fail(key, "must lie within [" + std::to_string(min) + ", " + std::to_string(max) + "]");
  }
  return {lo, hi};
}

std::pair<double, double> YamlSection::real_range(const std::string& key, double min, double max) {
  const YAML::Node v = require(key);
  if (!v.IsSequence() || v.size() != 2) fail(key, "expected [lo, hi]");
  double lo = 0.0;
  double hi = 0.0;
  try {
    lo = v[0].as<double>();
    hi = v[1].as<double>();
  } catch (const YAML::Exception&) {
    fail(key, "expected two numbers [lo, hi]");
  }
  if (!(lo <= hi)) fail(key, "lo must be <= hi");
  if (lo < min || hi > max) fail(key, "must lie within [" + num(min) + ", " + num(max) + "]");
  return {lo, hi};
}

std::vector<double> YamlSection::reals(const std::string& key, double lo, double hi) {
  const YAML::Node v = require(key);
  if (!v.IsSequence() || v.size() == 0) fail(key, "expected a non-empty list of numbers");
  std::vector<double> out;
  for (std::size_t i = 0; i < v.size(); ++i) {
    double x = 0.0;
    try {
      x = v[i].as<double>();
    } catch (const YAML::Exception&) {
      fail(key + "[" + std::to_string(i) + "]", "expected a number");
    }
    if (!std::isfinite(x) || x < lo || x > hi) {
      fail(key + "[" + std::to_string(i) + "]", "must be in [" + num(lo) + ", " + num(hi) + "]");
    }
    out.push_back(x);
  }
  return out;
}

std::vector<YamlSection> YamlSection::sections(const std::string& key) {
  const YAML::Node v = require(key);
  if (!v.IsSequence() || v.size() == 0) fail(key, "expected a non-empty list");
  std::vector<YamlSection> out;
  for (std::size_t i = 0; i < v.size(); ++i) {
    out.emplace_back(v[i], source_, full(key) + "[" + std::to_string(i) + "]");
  }
  return out;
}

std::vector<std::pair<std::string, YamlSection>> YamlSection::named_sections(
    const std::string& key) {
  const YAML::Node v = require(key);
  if (!v.IsMap() || v.size() == 0) fail(key, "expected a non-empty mapping");
  std::vector<std::pair<std::string, YamlSection>> out;
  for (const auto& kv : v) {
    const auto name = kv.first.as<std::string>();
    out.emplace_back(name, YamlSection(kv.second, source_, full(key) + "." + name));
  }
  return out;
}

void YamlSection::reject_unknown() const {
  for (const auto& kv : node_) {
    const auto k = kv.first.as<std::string>();
    if (!seen_.contains(k)) fail(k, "unknown key (typo?)");
  }
}

YAML::Node parse_yaml(const std::string& text, const std::string& source) {
  try {
    YAML::Node root = YAML::Load(text);
    if (!root || root.IsNull()) throw ConfigError(source, "", "file is empty");
    return root;
  } catch (const YAML::ParserException& e) {
    throw ConfigError(source, "", std::string{"invalid YAML: "} + e.what());
  }
}

std::string read_text_file(const std::filesystem::path& file, const std::string& source) {
  std::ifstream in(file, std::ios::binary);
  if (!in) throw ConfigError(source, "", "cannot open " + file.string());
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

}  // namespace legalvrp::detail
