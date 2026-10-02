#pragma once
// Internal helper (not a public API): strict reading of one YAML mapping.
// Tracks consumed keys, rejects unknown ones, and reports every problem as a
// ConfigError carrying the source file and the full dotted key.
// Include only from .cpp files of modules that link yaml-cpp privately.

#include <yaml-cpp/yaml.h>

#include <filesystem>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "legalvrp/domain/models.hpp"

namespace legalvrp::detail {

class YamlSection {
 public:
  YamlSection(const YAML::Node& node, std::string source, std::string prefix);

  [[noreturn]] void fail(const std::string& key, const std::string& reason) const;
  [[nodiscard]] std::string full(const std::string& key) const;
  [[nodiscard]] const std::string& source() const noexcept { return source_; }

  [[nodiscard]] bool has(const std::string& key) const { return static_cast<bool>(node_[key]); }
  YAML::Node require(const std::string& key);
  YamlSection child(const std::string& key);  // nested mapping

  long long integer(const std::string& key, long long lo, long long hi);
  Minutes minutes(const std::string& key, Minutes lo, Minutes hi);
  double real(const std::string& key, double lo, double hi);
  Euros euros(const std::string& key, double lo, double hi) { return real(key, lo, hi); }
  bool boolean(const std::string& key);
  std::string text(const std::string& key);

  // [lo, hi] pair with lo <= hi, each within [min, max].
  std::pair<long long, long long> integer_range(const std::string& key, long long min, long long max);
  std::pair<double, double> real_range(const std::string& key, double min, double max);
  std::vector<double> reals(const std::string& key, double lo, double hi);  // non-empty list

  // Sequence of mappings, each wrapped as "<key>[i]".
  std::vector<YamlSection> sections(const std::string& key);
  // Mapping of name -> mapping, each wrapped as "<key>.<name>", in file order.
  std::vector<std::pair<std::string, YamlSection>> named_sections(const std::string& key);

  void reject_unknown() const;

 private:
  YAML::Node node_;
  std::string source_;
  std::string prefix_;
  std::set<std::string> seen_;
};

// Parses YAML text; ParserException / empty document become ConfigError.
YAML::Node parse_yaml(const std::string& text, const std::string& source);
std::string read_text_file(const std::filesystem::path& file, const std::string& source);

}  // namespace legalvrp::detail
