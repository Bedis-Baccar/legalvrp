// T2 acceptance: same seed => byte-identical output; all s_i >= 1; matrices integer.
// Plus: portable RNG golden values, instance invariants over several seeds, JSON round-trip,
// instance-config validation.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <map>
#include <set>

#include "legalvrp/data/generate.hpp"
#include "legalvrp/data/instance_config.hpp"
#include "legalvrp/data/io.hpp"
#include "legalvrp/data/orders.hpp"
#include "legalvrp/data/rng.hpp"
#include "legalvrp/domain/json.hpp"
#include "legalvrp/domain/paths.hpp"
#include "legalvrp/domain/rules.hpp"
#include "legalvrp/estimate/deterministic.hpp"
#include "legalvrp/week/certify.hpp"

using namespace legalvrp;
using namespace legalvrp::data;
namespace fs = std::filesystem;
using nlohmann::json;

namespace {

const Config& config() {
  static const Config c = load_config();
  return c;
}
InstanceConfig instance_cfg(const char* name) {
  return load_instance_config(config_dir() / (std::string{"instance_"} + name + ".yaml"));
}

struct TempDir {
  fs::path path;
  explicit TempDir(const char* tag)
      : path(fs::temp_directory_path() / (std::string{"legalvrp_"} + tag)) {
    fs::remove_all(path);
    fs::create_directories(path);
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};

}  // namespace

// ============================================================ portable RNG

TEST_CASE("SplitMix64 matches the published reference", "[rng]") {
  std::uint64_t s = 0;
  CHECK(splitmix64(s) == UINT64_C(0xe220a8397b1dcdaf));
  CHECK(splitmix64(s) == UINT64_C(0x6e789e6aa1b965f4));
}

TEST_CASE("xoshiro256** golden values (independent bash reference)", "[rng]") {
  Rng a(0);
  CHECK(a.next_u64() == UINT64_C(0x99ec5f36cb75f2b4));
  CHECK(a.next_u64() == UINT64_C(0xbf6e1f784956452a));
  CHECK(a.next_u64() == UINT64_C(0x1a5f849d4933e6e0));
  CHECK(a.next_u64() == UINT64_C(0x6aa594f1262d2d2c));
  Rng b(42);
  CHECK(b.next_u64() == UINT64_C(0x15780b2e0c2ec716));
  CHECK(b.next_u64() == UINT64_C(0x6104d9866d113a7e));
  CHECK(b.next_u64() == UINT64_C(0xae17533239e499a1));
  CHECK(b.next_u64() == UINT64_C(0xecb8ad4703b360a1));
}

TEST_CASE("portable_exp is accurate", "[rng]") {
  for (const double x : {-20.0, -4.0, -2.5, -1.0, -0.5, -0.01, 0.0, 0.3, 1.0, 3.0}) {
    INFO("x = " << x);
    CHECK(portable_exp(x) == Catch::Approx(std::exp(x)).epsilon(1e-12));
  }
}

TEST_CASE("uniform_int is in range and unbiased", "[rng]") {
  Rng r(7);
  std::map<std::int64_t, int> count;
  constexpr int kDraws = 60000;
  for (int i = 0; i < kDraws; ++i) {
    const auto x = r.uniform_int(3, 8);
    REQUIRE(x >= 3);
    REQUIRE(x <= 8);
    ++count[x];
  }
  REQUIRE(count.size() == 6);
  for (const auto& [v, c] : count) CHECK(std::abs(c - kDraws / 6) < kDraws / 60);  // within 10 %
  CHECK(r.uniform_int(5, 5) == 5);
}

TEST_CASE("poisson has the right mean; categorical skips zero weights", "[rng]") {
  Rng r(11);
  for (const double lambda : {0.5, 2.0, 2.5}) {
    double sum = 0.0;
    constexpr int kDraws = 40000;
    for (int i = 0; i < kDraws; ++i) sum += r.poisson(lambda);
    CHECK(sum / kDraws == Catch::Approx(lambda).epsilon(0.03));
  }
  CHECK(r.poisson(0.0) == 0);
  const std::vector<double> w{0.0, 1.0, 0.0, 3.0};
  for (int i = 0; i < 2000; ++i) {
    const auto k = r.categorical(w);
    REQUIRE((k == 1 || k == 3));
  }
}

TEST_CASE("split streams are reproducible and distinct", "[rng]") {
  const Rng root(5);
  Rng a1 = root.split(1);
  Rng a2 = root.split(1);
  Rng b = root.split(2);
  const auto x = a1.next_u64();
  CHECK(x == a2.next_u64());
  CHECK(x != b.next_u64());
}

// ============================================================ determinism (T2 acceptance)

TEST_CASE("same seed gives byte-identical files; another seed differs", "[generate]") {
  const auto cfg = instance_cfg("small");
  TempDir t1("gen_a"), t2("gen_b"), t3("gen_c");
  const auto g1 = generate_week(cfg, config(), 1);
  const auto g2 = generate_week(cfg, config(), 1);
  const auto g3 = generate_week(cfg, config(), 2);
  write_week(t1.path, g1.week, g1.truth);
  write_week(t2.path, g2.week, g2.truth);
  write_week(t3.path, g3.week, g3.truth);
  for (const char* f : {"week.json", "matrix.json", "truth.json"}) {
    INFO(f);
    CHECK(read_text_file(t1.path / f) == read_text_file(t2.path / f));
  }
  CHECK(read_text_file(t1.path / "week.json") != read_text_file(t3.path / "week.json"));
}

TEST_CASE("generator reproduces the committed golden fixtures byte for byte", "[generate]") {
  // Cross-platform check: fixtures were produced on Windows/MSVC; CI regenerates them on
  // Linux/GCC. Regenerate after an intentional change with:
  //   legalvrp-generate --config config/instance_<name>.yaml --seed 1 --out tests/fixtures/instances/<name>/1
  // (certified, T5)
  for (const char* name : {"tiny", "small"}) {
    const fs::path golden = fixtures_dir() / "instances" / name / "1";
    REQUIRE(fs::exists(golden / "week.json"));
    const auto c = week::generate_certified_week(instance_cfg(name), config(), 1);
    REQUIRE(c.has_value());
    const auto& g = c->generated;
    TempDir t(name);
    write_week(t.path, g.week, g.truth);
    for (const char* f : {"week.json", "matrix.json", "truth.json"}) {
      INFO(name << "/" << f);
      CHECK(read_text_file(t.path / f) == read_text_file(golden / f));
    }
  }
}

// ============================================================ invariants

TEST_CASE("generated weeks satisfy every invariant (5 seeds x tiny/small)", "[generate]") {
  for (const char* name : {"tiny", "small"}) {
    const auto cfg = instance_cfg(name);
    for (std::uint64_t seed = 1; seed <= 5; ++seed) {
      INFO(name << " seed " << seed);
      const auto g = generate_week(cfg, config(), seed);
      const WeekInstance& w = g.week;
      CHECK(validate_week(w).empty());

      const Pallets largest =
          std::ranges::max(w.trucks, {}, &Truck::capacity_pallets).capacity_pallets;
      std::map<int, std::set<std::string>> customers_by_day;
      for (const auto& o : w.orders) {
        CHECK(o.service_mu >= 1);                             // T2: all s_i >= 1
        CHECK(o.service_mu == service_minutes(o.pallets));    // 10 + 6 * pallets
        CHECK(o.service_sigma == 0);
        CHECK(o.pallets >= 1);
        CHECK(o.pallets <= largest);
        const Customer& cu = *std::ranges::find(w.customers, o.customer_id, &Customer::id);
        const auto& tt = *std::ranges::find(cfg.customer_types, cu.type, &CustomerTypeParams::type);
        CHECK(g.truth.mean.at(o.id) == Catch::Approx(true_service_mean(tt, o.pallets)));  // V1-T6 truth model
        CHECK(g.truth.sigma.at(o.id) == Catch::Approx(tt.service_cv * g.truth.mean.at(o.id)).margin(1e-3));
        CHECK(g.truth.minutes.at(o.id) >= 1);
        CHECK(customers_by_day[o.day].insert(o.customer_id).second);  // <= 1 order/customer/day
      }
      for (int d = 0; d < w.days; ++d) {
        const auto n = static_cast<int>(customers_by_day[d].size());
        CHECK(n >= cfg.orders_min);
        CHECK(n <= cfg.orders_max);
      }
      for (const auto& c : w.customers) {
        const auto& tp = *std::ranges::find(cfg.customer_types, c.type, &CustomerTypeParams::type);
        CHECK(c.window_start == tp.window_start);
        CHECK(c.window_end == tp.window_end);
      }

      // Matrix: integer minutes (by type), access time on every leg, symmetric km,
      // asymmetric minutes somewhere (noise exercised).
      const Matrix& m = w.matrix;
      bool asymmetric = false;
      for (std::size_t i = 0; i < m.size(); ++i) {
        for (std::size_t j = 0; j < m.size(); ++j) {
          if (i == j) continue;
          CHECK(m.time(i, j) >= cfg.access_minutes_per_leg);
          CHECK(m.dist(i, j) == m.dist(j, i));
          asymmetric = asymmetric || m.time(i, j) != m.time(j, i);
        }
      }
      CHECK(asymmetric);
    }
  }
}

TEST_CASE("monday and thursday are heavier on average (weekday factor)", "[generate]") {
  const auto cfg = instance_cfg("small");
  std::map<int, int> total;
  for (std::uint64_t seed = 1; seed <= 30; ++seed) {
    for (const auto& o : generate_week(cfg, config(), seed).week.orders) ++total[o.day];
  }
  CHECK(total[0] > total[4]);  // Mon > Fri
  CHECK(total[3] > total[4]);  // Thu > Fri
}

TEST_CASE("validate_week reports broken data", "[generate]") {
  auto w = generate_week(instance_cfg("tiny"), config(), 1).week;
  w.orders[0].service_mu = 0;
  w.orders[1].customer_id = "nobody";
  w.matrix.time_min[1] = -3;
  const auto p = validate_week(w);
  const auto has = [&](const std::string& s) {
    return std::ranges::any_of(p, [&](const std::string& m) { return m.find(s) != std::string::npos; });
  };
  CHECK(has("service_mu must be >= 1"));
  CHECK(has("unknown customer nobody"));
  CHECK(has("negative entry"));
}

// ============================================================ JSON I/O

TEST_CASE("week round-trips through files unchanged", "[io]") {
  const auto g = generate_week(instance_cfg("small"), config(), 3);
  TempDir a("rt_a"), b("rt_b");
  write_week(a.path, g.week, g.truth);
  const WeekInstance back = read_week(a.path);
  write_week(b.path, back, read_truth(a.path));
  for (const char* f : {"week.json", "matrix.json", "truth.json"}) {
    INFO(f);
    CHECK(read_text_file(a.path / f) == read_text_file(b.path / f));
  }
  CHECK(read_text_file(a.path / "week.json").find('\r') == std::string::npos);  // LF only
}

TEST_CASE("plans round-trip, optional fields included", "[io]") {
  DayPlan p;
  p.day = 2;
  Route r;
  r.driver_id = "k1";
  r.order_ids = {"d2-o01", "d2-o02"};
  r.break_after_order_id = "d2-o01";
  r.departure = 400;
  r.arrivals = {430, 520};
  r.service_starts = {440, 520};
  r.return_time = 600;
  p.routes = {r, Route{"k2", {}, std::nullopt, 380, {}, {}, 380}};
  p.postponed_order_ids = {"d2-o03"};
  p.objective = 123.45;
  p.solver_stats.status = "OPTIMAL";

  const json j = p;
  CHECK_FALSE(j["routes"][1].contains("break_after_order_id"));
  const auto back = j.get<DayPlan>();
  CHECK(json(back) == j);
  CHECK(back.routes[0].break_after_order_id == "d2-o01");
  CHECK_FALSE(back.routes[1].break_after_order_id.has_value());
}

TEST_CASE("reading a matrix with non-integer minutes fails", "[io]") {
  json j = json(generate_week(instance_cfg("tiny"), config(), 1).week.matrix);
  j["time_min"][0][1] = 12.5;
  CHECK_THROWS(j.get<Matrix>());
}

// ============================================================ instance config validation

namespace {
std::string tiny_yaml() { return read_text_file(config_dir() / "instance_tiny.yaml"); }
std::string replaced(std::string s, const std::string& from, const std::string& to) {
  const auto pos = s.find(from);
  REQUIRE(pos != std::string::npos);
  return s.replace(pos, from.size(), to);
}
std::string error_key(const std::function<void()>& f) {
  try {
    f();
  } catch (const ConfigError& e) {
    INFO(e.what());
    return e.key();
  }
  FAIL("expected ConfigError");
  return {};
}
}  // namespace

TEST_CASE("instance config errors name the key", "[instance_config]") {
  CHECK(error_key([] { (void)parse_instance_config(replaced(tiny_yaml(), "speed_kmh: 55\n", ""), "t"); }) ==
        "speed_kmh");
  CHECK(error_key([] {
          (void)parse_instance_config(replaced(tiny_yaml(), "  site:", "  bakery:"), "t");
        }) == "customer_types.bakery");
  CHECK(error_key([] {
          (void)parse_instance_config(
              replaced(tiny_yaml(), "  - {capacity_pallets: 15, has_tail_lift: false}\n", ""), "t");
        }) == "trucks");
  CHECK(error_key([] {
          (void)parse_instance_config(replaced(tiny_yaml(), "orders_per_day: [6, 8]", "orders_per_day: [8, 6]"), "t");
        }) == "orders_per_day");
  CHECK(error_key([] {
          (void)parse_instance_config(
              replaced(tiny_yaml(), "pallets_lambda: 2.0, tail_lift_prob: 0.15",
                       "pallets_lambda: 2.0, tail_lift_prob: 1.5"), "t");
        }) == "customer_types.grocery.tail_lift_prob");
}

TEST_CASE("generator rejects configs inconsistent with rules or contracts", "[instance_config]") {
  auto cfg = instance_cfg("tiny");
  cfg.drivers[0].contract = "freelance";
  CHECK(error_key([&] { (void)generate_week(cfg, config(), 1); }) == "drivers[0].contract");

  cfg = instance_cfg("tiny");
  cfg.shift_span_max = 800;  // > 1440 - 660: daily rest no longer automatic (A12)
  CHECK(error_key([&] { (void)generate_week(cfg, config(), 1); }) == "shift_span_max");
}

// ============================================================ estimator (§9)

TEST_CASE("DeterministicEstimator returns service_mu with sigma 0", "[estimate]") {
  const auto w = generate_week(instance_cfg("tiny"), config(), 1).week;
  const estimate::DeterministicEstimator est;
  const auto e = est.estimate(w.orders, w.customers);
  REQUIRE(e.size() == w.orders.size());
  for (std::size_t i = 0; i < e.size(); ++i) {
    CHECK(e[i].mu == Catch::Approx(w.orders[i].service_mu));
    CHECK(e[i].sigma == 0.0);
  }
}

// ============================================================ scale instances (T9)

TEST_CASE("scale config: sizes, seeds, K = ceil(n/5), certified one-day instances", "[scale]") {
  const auto sc = load_scale_config(config_dir() / "instance_scale.yaml");
  CHECK(sc.orders == std::vector<int>{8, 10, 12, 15, 20, 25, 30, 40});
  CHECK(sc.seeds.size() == 5);
  CHECK(sc.solver_profile == "scale");
  for (const int n : {8, 12, 40}) {
    const auto ic = scale_instance(sc, n);
    CHECK(ic.days == 1);
    CHECK(ic.orders_min == n);
    CHECK(ic.orders_max == n);
    CHECK(static_cast<int>(ic.drivers.size()) == (n + 4) / 5);
    CHECK(ic.trucks.size() == ic.drivers.size());
  }
  const auto c = week::generate_certified_week(scale_instance(sc, 12), config(), 1);
  REQUIRE(c.has_value());
  CHECK(c->generated.week.orders.size() == 12);
  CHECK(c->generated.week.drivers.size() == 3);
}

// ============================================================ large instances (V1-T0)

namespace {
std::uint64_t fnv1a(const std::string& s) {
  std::uint64_t h = UINT64_C(0xcbf29ce484222325);
  for (const char ch : s) {
    h ^= static_cast<unsigned char>(ch);
    h *= UINT64_C(0x100000001b3);
  }
  return h;
}
}  // namespace

TEST_CASE("large family: 60/70/80 orders, K = 12/14/16, five-day weeks", "[large]") {
  const auto lc = load_scale_config(config_dir() / "instance_large.yaml");
  CHECK(lc.name == "large");
  CHECK(lc.orders == std::vector<int>{60, 70, 80});
  CHECK(lc.days == 5);
  for (const int n : lc.orders) {
    const auto ic = scale_instance(lc, n);
    CHECK(ic.name == "large_n" + std::to_string(n));
    CHECK(ic.days == 5);
    CHECK(static_cast<int>(ic.drivers.size()) == n / 5);
    CHECK(ic.weekday_factor.size() >= 5);
  }
  // The T9 scale family is unchanged: one-day instances named scale_n<n>.
  const auto sc = load_scale_config(config_dir() / "instance_scale.yaml");
  CHECK(sc.days == 1);
  CHECK(scale_instance(sc, 8).name == "scale_n8");
}

TEST_CASE("large family: every size x seed certifies (V1-T0 acceptance)", "[large]") {
  const auto lc = load_scale_config(config_dir() / "instance_large.yaml");
  for (const int n : lc.orders) {
    for (const auto seed : lc.seeds) {
      INFO("n " << n << " seed " << seed);
      const auto c = week::generate_certified_week(scale_instance(lc, n), config(), seed);
      REQUIRE(c.has_value());
      CHECK(c->generated.week.orders.size() == static_cast<std::size_t>(5 * n));
      CHECK(c->baseline.check.ok());
    }
  }
}

TEST_CASE("large family: byte-identical output, golden hash checked on every platform", "[large]") {
  const auto lc = load_scale_config(config_dir() / "instance_large.yaml");
  const auto a = week::generate_certified_week(scale_instance(lc, 60), config(), 1);
  const auto b = week::generate_certified_week(scale_instance(lc, 60), config(), 1);
  REQUIRE(a.has_value());
  REQUIRE(b.has_value());
  const std::string wa = to_canonical_text(nlohmann::json(a->generated.week));
  const std::string ma = to_canonical_text(nlohmann::json(a->generated.week.matrix));
  CHECK(wa == to_canonical_text(nlohmann::json(b->generated.week)));
  // Golden values computed on Windows/MSVC; CI recomputes them on Linux/GCC.
  CHECK(fnv1a(wa) == UINT64_C(0x16c6aa75ecfcd5b5));
  CHECK(fnv1a(ma) == UINT64_C(0xc1a39e033ac8937b));
}
