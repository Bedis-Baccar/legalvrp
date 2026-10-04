// V1-T10: Gurobi-free ALNS regression on fixed seeds, checked on every platform by CI. ALNS with
// an iteration limit is deterministic, and every number it uses is platform-independent (portable
// RNG, exp and log; exact IEEE arithmetic; tie-broken sorts), so the cost AND the routes of these
// runs must be identical on Windows/MSVC (where the golden values were computed) and Linux/GCC.
// A change of ALNS that alters its search changes these values: update them deliberately.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "legalvrp/alns/alns.hpp"
#include "legalvrp/check/checker.hpp"
#include "legalvrp/data/generate.hpp"
#include "legalvrp/data/instance_config.hpp"
#include "legalvrp/data/io.hpp"
#include "legalvrp/domain/day.hpp"
#include "legalvrp/domain/paths.hpp"
#include "legalvrp/domain/rules.hpp"

using namespace legalvrp;

namespace {

std::uint64_t route_hash(const std::vector<std::vector<std::size_t>>& routes) {
  std::uint64_t h = UINT64_C(1469598103934665603);  // FNV-1a over (driver, stops, separator)
  auto mix = [&](std::uint64_t v) {
    for (int b = 0; b < 8; ++b) {
      h ^= (v >> (8 * b)) & 0xffU;
      h *= UINT64_C(1099511628211);
    }
  };
  for (std::size_t k = 0; k < routes.size(); ++k) {
    mix(k);
    for (const auto i : routes[k]) mix(i);
    mix(UINT64_C(0xffffffffffffffff));
  }
  return h;
}

struct Case {
  std::string name;
  DayInstance day;
  long long iterations;
  double cost;          // golden
  std::uint64_t routes; // golden
};

std::vector<Case> cases() {
  const Config cfg = load_config();
  std::vector<Case> out;
  const auto tiny = data::read_week(fixtures_dir() / "instances" / "tiny" / "1");
  out.push_back({"tiny day 0", make_day_instance(tiny, 0), 300, 690.84280000000001, UINT64_C(0xb981ce9041518473)});
  const auto small = data::read_week(fixtures_dir() / "instances" / "small" / "1");
  const double small_cost[] = {783.60960000000011, 933.55960000000027, 892.63160000000016, 879.2632000000001,
                               597.25480000000005};
  const std::uint64_t small_routes[] = {UINT64_C(0x3adb1563bd1d80f3), UINT64_C(0xa335854c9e38be20),
                                        UINT64_C(0xdc0a68e46fb3a688), UINT64_C(0x75c3c769e009fa53),
                                        UINT64_C(0xf0f2fe9ea0473fac)};
  for (int d = 0; d < small.days; ++d) {
    out.push_back({"small day " + std::to_string(d), make_day_instance(small, d), 1000,
                   small_cost[static_cast<std::size_t>(d)], small_routes[static_cast<std::size_t>(d)]});
  }
  const auto lc = data::load_scale_config(config_dir() / "instance_large.yaml");
  const auto large = data::generate_week(data::scale_instance(lc, 60), cfg, 1).week;  // golden-hash instance
  out.push_back({"large n60 day 0", make_day_instance(large, 0), 300, 2355.3015999999998, UINT64_C(0xfcb0752ab1f23e33)});
  return out;
}

}  // namespace

TEST_CASE("ALNS regression on fixed seeds: same cost and routes on every platform", "[alns][regression]") {
  for (const auto& c : cases()) {
    alns::Options o;
    o.max_iterations = c.iterations;
    o.time_limit_s = 1e9;
    o.seed = 1;
    const auto r = alns::solve(c.day, o);
    std::ostringstream got;
    got << std::setprecision(17) << r.cost << ", UINT64_C(0x" << std::hex << route_hash(r.routes) << ")";
    INFO(c.name << ": got " << got.str());
    CHECK(check::check_day(c.day, r.plan).ok());
    CHECK(r.cost == Catch::Approx(c.cost).epsilon(1e-12));
    CHECK(route_hash(r.routes) == c.routes);
  }
}
