#pragma once
// legalvrp::domain — a broken rule, and the rule keys shared by the route evaluator (T3)
// and the independent checker (T4). Keys follow config/rules.yaml where a rule has one.

#include <string>
#include <string_view>

namespace legalvrp {

namespace rule {
inline constexpr std::string_view coverage = "coverage";                // served once or postponed
inline constexpr std::string_view tail_lift = "tail_lift";
inline constexpr std::string_view capacity = "capacity";
inline constexpr std::string_view time_window = "time_window";
inline constexpr std::string_view shift = "shift";                      // departure / return vs shift
inline constexpr std::string_view schedule = "schedule";                // windows + shift jointly infeasible
inline constexpr std::string_view consistency = "consistency";          // reported times vs durations
inline constexpr std::string_view drive_before_break = "drive_before_break";
inline constexpr std::string_view drive_after_break = "drive_after_break";
inline constexpr std::string_view drive_without_break = "drive_without_break";
inline constexpr std::string_view work_before_break = "work_before_break";
inline constexpr std::string_view work_after_break = "work_after_break";
inline constexpr std::string_view work_between_breaks = "work_between_breaks";     // V1-T8: between two breaks
inline constexpr std::string_view work_without_break = "work_without_break";
inline constexpr std::string_view drive_between_breaks = "drive_between_breaks";   // V1-T8
inline constexpr std::string_view break_too_short = "break_too_short";             // V1-T8: 30 / 45 min (W2)
inline constexpr std::string_view daily_drive_max = "daily_drive_max";
inline constexpr std::string_view daily_service_max = "daily_service_max";
inline constexpr std::string_view weekly_drive_max = "weekly_drive_max";
inline constexpr std::string_view weekly_service_max = "weekly_service_max";
inline constexpr std::string_view daily_rest_min = "daily_rest_min";
}  // namespace rule

struct Violation {
  std::string rule;       // one of legalvrp::rule
  std::string driver_id;
  std::string order_id;   // order concerned (or the break node), empty if route-level
  double amount = 0.0;    // excess in minutes / pallets when measurable, else 0
  int day = -1;           // set by week-level checks
};

}  // namespace legalvrp
