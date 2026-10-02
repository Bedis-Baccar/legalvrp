#include "legalvrp/domain/models.hpp"

#include <algorithm>
#include <stdexcept>

namespace legalvrp {

namespace {
template <class Range, class Key>
const auto& find_by(const Range& items, const std::string& wanted, Key key, const char* what) {
  const auto it = std::ranges::find_if(items, [&](const auto& x) { return key(x) == wanted; });
  if (it == items.end()) {
    throw std::out_of_range(std::string{"unknown "} + what + " '" + wanted + "'");
  }
  return *it;
}
}  // namespace

std::size_t Matrix::index_of(const std::string& node_id) const {
  const auto it = std::ranges::find(node_ids, node_id);
  if (it == node_ids.end()) throw std::out_of_range("unknown matrix node '" + node_id + "'");
  return static_cast<std::size_t>(it - node_ids.begin());
}

const Customer& DayInstance::customer(const std::string& id) const {
  return find_by(customers, id, [](const Customer& c) -> const std::string& { return c.id; },
                 "customer");
}

const Truck& DayInstance::truck(const std::string& id) const {
  return find_by(trucks, id, [](const Truck& t) -> const std::string& { return t.id; }, "truck");
}

const Contract& DayInstance::contract(const std::string& name) const {
  return find_by(contracts, name, [](const Contract& c) -> const std::string& { return c.name; },
                 "contract class");
}

const DriverWeekState& DayInstance::state(const std::string& driver_id) const {
  return find_by(states, driver_id,
                 [](const DriverWeekState& s) -> const std::string& { return s.driver_id; },
                 "driver state");
}

}  // namespace legalvrp
