#pragma once
// legalvrp::data — instance files.
//   <dir>/week.json    WeekInstance without the matrix (rules, contracts, costs embedded)
//   <dir>/matrix.json  node_ids, time_min (integer rows), dist_km (rows)
//   <dir>/truth.json   generator side, never read by the planner (V1-T6): per order the realised
//                      service minutes ("service_true") and the true distribution ("service_mean",
//                      "service_sigma"); equal to service_mu with sigma 0 when a config has no
//                      service_true model (V0)
// Files are written in binary mode with LF line endings: byte-identical on every OS.

#include <filesystem>
#include <map>
#include <string>

#include "legalvrp/domain/models.hpp"

namespace legalvrp::data {

struct TrueService {
  std::map<std::string, Minutes> minutes;  // realised service minutes per order
  std::map<std::string, double> mean;      // true mean (generator)
  std::map<std::string, double> sigma;     // true standard deviation (generator)
};

void write_text_file(const std::filesystem::path& file, const std::string& text);
[[nodiscard]] std::string read_text_file(const std::filesystem::path& file);

void write_week(const std::filesystem::path& dir, const WeekInstance& week,
                const TrueService& truth);
[[nodiscard]] WeekInstance read_week(const std::filesystem::path& dir);
[[nodiscard]] TrueService read_truth(const std::filesystem::path& dir);

}  // namespace legalvrp::data
