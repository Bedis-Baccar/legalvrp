// D-019 postponement penalties (domain level).
#include <catch2/catch_test_macros.hpp>

#include "legalvrp/domain/day.hpp"

using namespace legalvrp;

namespace {
Costs costs() { return Costs{1.6, 200.0, 2.0, 1000.0}; }
Order order(int day, std::optional<int> from = std::nullopt) {
  Order o;
  o.id = "o";
  o.day = day;
  o.postponed_from = from;
  o.postpone_penalty = 200.0;
  return o;
}
}  // namespace

TEST_CASE("postponement penalty escalates per day carried", "[day][postpone]") {
  constexpr int kLast = 4;  // Friday
  CHECK(effective_postpone_penalty(costs(), order(0), 0, kLast) == 200.0);
  CHECK(effective_postpone_penalty(costs(), order(0, 0), 1, kLast) == 400.0);
  CHECK(effective_postpone_penalty(costs(), order(0, 0), 2, kLast) == 800.0);
}

TEST_CASE("last day: postponing means unserved this week", "[day][postpone]") {
  constexpr int kLast = 4;
  CHECK(effective_postpone_penalty(costs(), order(4), 4, kLast) == 1000.0);       // fresh Friday order
  CHECK(effective_postpone_penalty(costs(), order(1, 1), 4, kLast) == 1600.0);    // 200 * 2^3 > 1000
}

TEST_CASE("make_day_instance applies the effective penalty", "[day][postpone]") {
  WeekInstance w;
  w.days = 5;
  w.costs = costs();
  Order fresh = order(2);
  fresh.id = "fresh";
  w.orders = {fresh};
  Order carried = order(0, 0);
  carried.id = "carried";
  const DayInstance d = make_day_instance(w, 2, {carried});
  REQUIRE(d.orders.size() == 2);
  CHECK(d.orders[0].id == "carried");
  CHECK(d.orders[0].postpone_penalty == 800.0);
  CHECK(d.orders[1].postpone_penalty == 200.0);
}
