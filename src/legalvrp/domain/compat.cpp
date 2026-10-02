#include "legalvrp/domain/compat.hpp"

namespace legalvrp {

bool compatible(const Truck& truck, const Customer& customer, const Order& order) noexcept {
  if (customer.needs_tail_lift && !truck.has_tail_lift) return false;
  return order.pallets <= truck.capacity_pallets;
}

std::vector<std::vector<std::size_t>> compatible_orders(const DayInstance& day) {
  std::vector<std::vector<std::size_t>> out(day.drivers.size());
  for (std::size_t k = 0; k < day.drivers.size(); ++k) {
    const Truck& truck = day.truck(day.drivers[k].truck_id);
    for (std::size_t i = 0; i < day.orders.size(); ++i) {
      if (compatible(truck, day.customer(day.orders[i].customer_id), day.orders[i])) {
        out[k].push_back(i);
      }
    }
  }
  return out;
}

}  // namespace legalvrp
