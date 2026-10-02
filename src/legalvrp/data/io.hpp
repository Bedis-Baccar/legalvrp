#pragma once
// legalvrp::data — instance files.
//   <dir>/week.json    WeekInstance without the matrix (rules, contracts, costs embedded)
//   <dir>/matrix.json  node_ids, time_min (integer rows), dist_km (rows)
//   <dir>/truth.json   true service minutes per order (equal to service_mu in V0, §9)
// Files are written in binary mode with LF line endings: byte-identical on every OS.

#include <filesystem>
#include <map>
#include <string>

#include "legalvrp/domain/models.hpp"

namespace legalvrp::data {

using TrueService = std::map<std::string, Minutes>;  // order id -> minutes

void write_text_file(const std::filesystem::path& file, const std::string& text);
[[nodiscard]] std::string read_text_file(const std::filesystem::path& file);

void write_week(const std::filesystem::path& dir, const WeekInstance& week,
                const TrueService& truth);
[[nodiscard]] WeekInstance read_week(const std::filesystem::path& dir);
[[nodiscard]] TrueService read_truth(const std::filesystem::path& dir);

}  // namespace legalvrp::data
