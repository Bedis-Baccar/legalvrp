#pragma once
// legalvrp::domain — driver/order compatibility (§4, D-008).
// A driver can serve an order iff
//   * the truck has a tail-lift when the customer needs one, and
//   * the order's pallets fit in the truck (pallets <= capacity).
// There are no access classes: truck size never restricts a customer (D-008).
// Incompatible pairs create no MILP variables (§4).

#include <cstddef>
#include <vector>

#include "legalvrp/domain/models.hpp"

namespace legalvrp {

[[nodiscard]] bool compatible(const Truck& truck, const Customer& customer,
                              const Order& order) noexcept;

// For each driver of the day (same order as day.drivers), the indices into
// day.orders of the orders that driver can serve. Throws std::out_of_range if a
// driver's truck or an order's customer is unknown.
[[nodiscard]] std::vector<std::vector<std::size_t>> compatible_orders(const DayInstance& day);

}  // namespace legalvrp
