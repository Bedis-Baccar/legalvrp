#include "legalvrp/data/roster.hpp"

#include <string>

namespace legalvrp::data {

Roster make_roster(const InstanceConfig& cfg) {
  Roster r;
  std::vector<int> all_days;
  for (int d = 0; d < cfg.days; ++d) all_days.push_back(d);

  for (std::size_t i = 0; i < cfg.drivers.size(); ++i) {
    const std::string n = std::to_string(i + 1);
    r.trucks.push_back(Truck{"t" + n, cfg.trucks[i].capacity_pallets, cfg.trucks[i].has_tail_lift});
    r.drivers.push_back(Driver{"k" + n, cfg.drivers[i].contract, "t" + n,
                               cfg.drivers[i].shift_start,
                               cfg.drivers[i].shift_start + cfg.shift_span_max, all_days});
  }
  return r;
}

}  // namespace legalvrp::data
