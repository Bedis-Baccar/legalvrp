#pragma once
// legalvrp::kpi — week_kpis.json and week_report.md (PROJECT_BRIEF §11).

#include <filesystem>
#include <string>

#include <nlohmann/json.hpp>

#include "legalvrp/kpi/kpis.hpp"

namespace legalvrp::kpi {

[[nodiscard]] nlohmann::json to_json(const WeekKpis& k);
[[nodiscard]] std::string to_markdown(const WeekKpis& k, const std::string& title);

// Writes <dir>/week_kpis.json and <dir>/week_report.md (LF, byte-stable).
void write_week_report(const std::filesystem::path& dir, const WeekKpis& k, const std::string& title);

}  // namespace legalvrp::kpi
