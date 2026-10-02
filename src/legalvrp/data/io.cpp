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
  write_text_file(dir / "truth.json", to_canonical_text(json{{"service_true", truth}}));
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
  return json::parse(read_text_file(dir / "truth.json")).at("service_true").get<TrueService>();
}

}  // namespace legalvrp::data
