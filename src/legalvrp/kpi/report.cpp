#include "legalvrp/kpi/report.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>

#include "legalvrp/domain/json.hpp"

namespace legalvrp::kpi {

using nlohmann::json;

namespace {
std::string f1(double v, int prec = 1) {
  std::ostringstream os;
  os << std::fixed << std::setprecision(prec) << v;
  return os.str();
}
void write_file(const std::filesystem::path& p, const std::string& text) {
  std::ofstream out(p, std::ios::binary | std::ios::trunc);
  out.write(text.data(), static_cast<std::streamsize>(text.size()));
}
}  // namespace

json to_json(const WeekKpis& k) {
  json days = json::array();
  for (const auto& d : k.days) {
    days.push_back({{"day", d.day}, {"orders", d.orders}, {"served", d.served}, {"postponed", d.postponed},
                    {"drivers_used", d.drivers_used}, {"km", d.km}, {"cost", d.cost},
                    {"postponement_cost", d.postponement_cost}, {"service_minutes", d.service_minutes},
                    {"solver", d.solver}});
  }
  json drivers = json::array();
  for (const auto& d : k.drivers) {
    drivers.push_back({{"driver_id", d.driver_id}, {"contract", d.contract},
                       {"service_hours_week", d.service_minutes / 60.0},
                       {"driving_hours_week", d.driving_minutes / 60.0}, {"threshold_hours", d.threshold / 60.0},
                       {"extra_minutes", d.extra_minutes}, {"days_worked", d.days_worked}});
  }
  return {{"instance", k.instance}, {"seed", k.seed}, {"cost_total", k.cost_total}, {"km", k.km},
          {"served", k.served}, {"postponement_decisions", k.postponement_decisions},
          {"unserved_end", k.unserved_end}, {"unserved_order_ids", k.unserved_order_ids},
          {"on_time_rate", k.on_time_rate}, {"hours_gini", k.hours_gini},
          {"extra_minutes_total", k.extra_minutes_total}, {"week_checker_ok", k.week_checker_ok},
          {"violations", k.violations}, {"days", days}, {"drivers", drivers}};
}

std::string to_markdown(const WeekKpis& k, const std::string& title) {
  std::ostringstream md;
  md << "# " << title << "\n\n"
     << "Instance `" << k.instance << "` seed " << k.seed << ". All figures are recomputed by the independent "
     << "checker from the extracted routes. Week-mode checker: **"
     << (k.week_checker_ok ? "OK, 0 violations" : std::to_string(k.violations) + " VIOLATIONS") << "**.\n\n"
     << "## Week\n\n| KPI | value |\n|---|---|\n"
     << "| cost_total (EUR) | " << f1(k.cost_total, 2) << " |\n"
     << "| km | " << f1(k.km) << " |\n"
     << "| orders served | " << k.served << " |\n"
     << "| postponement decisions | " << k.postponement_decisions << " |\n"
     << "| orders unserved at the end of the week | " << k.unserved_end << " |\n"
     << "| on_time_rate | " << f1(k.on_time_rate, 3) << " |\n"
     << "| hours_gini (full-time drivers) | " << f1(k.hours_gini, 3) << " |\n"
     << "| extra minutes above weekly thresholds | " << k.extra_minutes_total << " |\n\n"
     << "## Days\n\n| day | orders | served | postponed | drivers | km | cost (EUR) | solver | time (s) | gap |\n"
     << "|---|---|---|---|---|---|---|---|---|---|\n";
  for (const auto& d : k.days) {
    md << "| " << d.day << " | " << d.orders << " | " << d.served << " | " << d.postponed << " | "
       << d.drivers_used << " | " << f1(d.km) << " | " << f1(d.cost, 2) << " | "
       << (d.solver.status.empty() ? "-" : d.solver.status) << (d.solver.baseline_fallback ? " (fallback)" : "")
       << " | " << f1(d.solver.runtime_s) << " | " << f1(100.0 * d.solver.gap) << "% |\n";
  }
  md << "\n## Drivers\n\n| driver | contract | service h (week) | driving h | threshold h | extra min | days |\n"
     << "|---|---|---|---|---|---|---|\n";
  for (const auto& d : k.drivers) {
    md << "| " << d.driver_id << " | " << d.contract << " | " << f1(d.service_minutes / 60.0) << " | "
       << f1(d.driving_minutes / 60.0) << " | " << f1(d.threshold / 60.0) << " | " << d.extra_minutes << " | "
       << d.days_worked << " |\n";
  }
  return md.str();
}

void write_week_report(const std::filesystem::path& dir, const WeekKpis& k, const std::string& title) {
  std::filesystem::create_directories(dir);
  write_file(dir / "week_kpis.json", to_canonical_text(to_json(k)));
  write_file(dir / "week_report.md", to_markdown(k, title));
}

}  // namespace legalvrp::kpi
