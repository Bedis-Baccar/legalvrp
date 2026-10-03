#pragma once
// legalvrp::sim — the day as it really happens (V1-T6).
//
// A plan made with the planning estimates (service_mu) is executed with the TRUE service
// minutes. Each driver leaves at the planned departure, keeps the planned sequence and break
// position, and starts every service at
//     max(arrival, planned service start)
// (an early arrival waits for the planned appointment; a late one absorbs the planned slack;
// nothing is re-optimised during the day). The realised day (orders' service_mu := true minutes)
// and the realised plan are then judged by the independent checker:
//   late     a service starts after the customer's window end (rule time_window)
//   illegal  any other rule broken in reality: shift end, 6-h work blocks, daily / weekly
//            service caps, ... (driving is unchanged: same sequence, same matrix)
// Weekly caps use the planning instance's state (the hours planned before this day).

#include <map>
#include <string>
#include <vector>

#include "legalvrp/check/checker.hpp"
#include "legalvrp/data/io.hpp"
#include "legalvrp/data/rng.hpp"
#include "legalvrp/domain/models.hpp"

namespace legalvrp::sim {

struct DutyOutcome {
  std::string driver_id;
  int stops = 0;
  int late_stops = 0;
  bool late = false;
  bool illegal = false;
  std::vector<std::string> rules;  // broken rules other than time_window
  Minutes max_delay = 0;           // realised minus planned service start, max over the stops
  Minutes overrun = 0;             // realised minus planned return to the depot
};

struct RealisedDay {
  DayInstance day;                 // service_mu = true minutes
  DayPlan plan;                    // realised times
  check::DayCheck check;           // the checker's verdict on the realised day
  std::vector<DutyOutcome> duties; // one per route of the plan
};

// `service_true`: minutes per order id; orders missing from it keep service_mu.
[[nodiscard]] RealisedDay realise_day(const DayInstance& day, const DayPlan& plan,
                                      const std::map<std::string, Minutes>& service_true);

// One scenario of true minutes for every order of `truth` (lognormal around the true mean,
// cv = sigma / mean), drawn in order-id order from `rng`.
[[nodiscard]] std::map<std::string, Minutes> sample_truth(const data::TrueService& truth, data::Rng& rng);

}  // namespace legalvrp::sim
