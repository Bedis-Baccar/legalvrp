// T1: driver/order compatibility (§4, D-008: no access classes).
#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

#include "legalvrp/domain/compat.hpp"

using namespace legalvrp;

namespace {
Customer customer(const char* id, bool tail_lift) {
  Customer c;
  c.id = id;
  c.window_start = 360;
  c.window_end = 600;
  c.needs_tail_lift = tail_lift;
  return c;
}
Order order(const char* id, const char* cust, Pallets pallets) {
  Order o;
  o.id = id;
  o.customer_id = cust;
  o.pallets = pallets;
  o.service_mu = 10 + 6 * pallets;
  return o;
}
}  // namespace

TEST_CASE("tail-lift is required only when the customer needs it", "[compat]") {
  const Truck lift{"t1", 12, true};
  const Truck plain{"t2", 12, false};
  const Customer needs = customer("c1", true);
  const Customer any = customer("c2", false);
  const Order o = order("o1", "c1", 4);

  CHECK(compatible(lift, needs, o));
  CHECK_FALSE(compatible(plain, needs, o));
  CHECK(compatible(lift, any, o));
  CHECK(compatible(plain, any, o));
}

TEST_CASE("capacity boundary: pallets == capacity fits, capacity + 1 does not", "[compat]") {
  const Truck t{"t1", 12, false};
  const Customer c = customer("c1", false);
  CHECK(compatible(t, c, order("o1", "c1", 12)));
  CHECK_FALSE(compatible(t, c, order("o2", "c1", 13)));
}

TEST_CASE("compatible_orders builds the C_k sets per driver", "[compat]") {
  DayInstance day;
  day.customers = {customer("c1", true), customer("c2", false)};
  day.trucks = {Truck{"small_lift", 12, true}, Truck{"big_plain", 18, false}};
  day.drivers = {Driver{"d1", "full_time", "small_lift", 360, 1125, {0}},
                 Driver{"d2", "full_time", "big_plain", 360, 1125, {0}}};
  day.orders = {order("o1", "c1", 4),     // needs tail-lift -> d1 only
                order("o2", "c2", 15),    // 15 pallets -> d2 only
                order("o3", "c2", 5)};    // both

  const auto ck = compatible_orders(day);
  REQUIRE(ck.size() == 2);
  CHECK(ck[0] == std::vector<std::size_t>{0, 2});
  CHECK(ck[1] == std::vector<std::size_t>{1, 2});
}

TEST_CASE("unknown truck or customer is a data error naming the id", "[compat]") {
  DayInstance day;
  day.customers = {customer("c1", false)};
  day.trucks = {Truck{"t1", 12, false}};
  day.drivers = {Driver{"d1", "full_time", "t_missing", 360, 1125, {0}}};
  day.orders = {order("o1", "c1", 4)};
  CHECK_THROWS_AS(compatible_orders(day), std::out_of_range);

  day.drivers[0].truck_id = "t1";
  day.orders[0].customer_id = "c_missing";
  try {
    (void)compatible_orders(day);
    FAIL("expected std::out_of_range");
  } catch (const std::out_of_range& e) {
    CHECK(std::string{e.what()}.find("c_missing") != std::string::npos);
  }
}
