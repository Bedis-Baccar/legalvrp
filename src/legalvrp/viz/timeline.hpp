#pragma once
// legalvrp::viz — what the visual application draws (docs/APP_PLAN.md, A1).
//
// The timeline of a duty, minute by minute, as the driver executes the reported plan: the
// same walk as the checker (fixed duty start D-018, departure, travel from the matrix, waiting,
// service, each break right after the service of its order, return, post-trip close). Times are
// recomputed from the departure, the service starts and the durations, never copied from the
// reported arrivals, so an inconsistent plan is drawn as it would really unfold.
//
// Counters, at the end of each segment:
//   drive_since_break  driving since the last break that restarts the 4 h 30 count (>= 45 min, or
//                      the 30-min part of a 15 + 30 split; Reg. 561/2006 art. 7)
//   work_since_break   work since the duty start or the last break (Dir. 2002/15/EC art. 5)
// Totals: driving and work (temps de service = duty end - duty start - breaks), equal to the
// checker's DriverFacts (tested).

#include <string>
#include <string_view>
#include <vector>

#include "legalvrp/domain/models.hpp"

namespace legalvrp::viz {

namespace segment {
inline constexpr std::string_view idle = "idle";        // on duty at the depot before the pre-trip work
inline constexpr std::string_view prep = "prep";        // pre-trip work (P)
inline constexpr std::string_view drive = "drive";
inline constexpr std::string_view wait = "wait";        // early at a customer, waiting for the planned start
inline constexpr std::string_view service = "service";
inline constexpr std::string_view rest = "break";       // a legal break (45, 30 or 15 min)
inline constexpr std::string_view close = "close";      // post-trip work (R)
}  // namespace segment

struct Segment {
  std::string kind;              // one of segment::*
  Minutes start = 0;
  Minutes end = 0;
  std::string order_id;          // service / wait / break: the order; drive: the destination order (empty = depot)
  Minutes drive_since_break = 0;
  Minutes work_since_break = 0;
};

struct Timeline {
  std::string driver_id;
  std::vector<Segment> segments;  // contiguous, from the duty start to the duty end
  Minutes duty_start = 0;
  Minutes duty_end = 0;
  Minutes driving = 0;
  Minutes work = 0;               // temps de service
  Minutes breaks = 0;
  double km = 0.0;
  Pallets pallets = 0;
};

// `route` must be a route of `day` (known driver and orders); an empty route gives an empty timeline.
[[nodiscard]] Timeline build_timeline(const DayInstance& day, const Route& route);

}  // namespace legalvrp::viz
