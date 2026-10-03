#include "legalvrp/data/io.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

#include "legalvrp/domain/json.hpp"

namespace legalvrp::data {

namespace fs = std::filesystem;
using nlohmann::json;

void write_text_file(const fs::path& file, const std::string& text) {
  if (file.has_parent_path()) fs::create_directories(file.parent_path());
  std::ofstream out(file, std::ios::binary | std::ios::trunc);
  if (!out) throw std::runtime_error("cannot write " + file.string());
  out.write(text.data(), static_cast<std::streamsize>(text.size()));
  if (!out) throw std::runtime_error("write failed: " + file.string());
}

std::string read_text_file(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  if (!in) throw std::runtime_error("cannot open " + file.string());
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

void write_week(const fs::path& dir, const WeekInstance& week, const TrueService& truth) {
  write_text_file(dir / "week.json", to_canonical_text(json(week)));
  write_text_file(dir / "matrix.json", to_canonical_text(json(week.matrix)));
  write_text_file(dir / "truth.json", to_canonical_text(json{{"service_true", truth.minutes},
                                                             {"service_mean", truth.mean},
                                                             {"service_sigma", truth.sigma}}));
}

WeekInstance read_week(const fs::path& dir) {
  try {
    WeekInstance w = json::parse(read_text_file(dir / "week.json")).get<WeekInstance>();
    w.matrix = json::parse(read_text_file(dir / "matrix.json")).get<Matrix>();
    return w;
  } catch (const json::exception& e) {
    throw std::runtime_error("reading instance " + dir.string() + ": " + e.what());
  }
}

TrueService read_truth(const fs::path& dir) {
  const json j = json::parse(read_text_file(dir / "truth.json"));
  TrueService t;
  t.minutes = j.at("service_true").get<std::map<std::string, Minutes>>();
  if (j.contains("service_mean")) {
    t.mean = j.at("service_mean").get<std::map<std::string, double>>();
    t.sigma = j.at("service_sigma").get<std::map<std::string, double>>();
  } else {  // V0 file: deterministic truth
    for (const auto& [id, m] : t.minutes) {
      t.mean[id] = static_cast<double>(m);
      t.sigma[id] = 0.0;
    }
  }
  return t;
}

}  // namespace legalvrp::data
