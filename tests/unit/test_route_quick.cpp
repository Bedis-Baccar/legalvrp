// V1-T1 acceptance: the fast evaluator (RouteEvaluator::quick) gives the same legality,
// temps de service, driving, km, cost and break pattern (V1-T8 included) as the exact STN evaluator on 100 000
// random routes and moves, and is much faster.
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <vector>

#include "legalvrp/data/generate.hpp"
#include "legalvrp/data/instance_config.hpp"
#include "legalvrp/data/io.hpp"
#include "legalvrp/data/rng.hpp"
#include "legalvrp/domain/day.hpp"
#include "legalvrp/domain/paths.hpp"
#include "legalvrp/domain/rules.hpp"
#include "legalvrp/heuristics/route_eval.hpp"
#include "legalvrp/heuristics/territory.hpp"

using namespace legalvrp;
using heuristics::RouteEvaluator;

namespace {

struct Case {
  std::size_t day;
  std::size_t driver;
  std::vector<std::size_t> seq;
};

std::vector<DayInstance> test_days() {
  const Config cfg = load_config();
  std::vector<DayInstance> days;
  const auto lc = data::load_scale_config(config_dir() / "instance_large.yaml");
  const auto large = data::generate_week(data::scale_instance(lc, 60), cfg, 1).week;
  const auto small = data::read_week(fixtures_dir() / "instances" / "small" / "1");
  data::Rng rng(77);
  for (const WeekInstance* w : {&large, &small}) {
    for (int d = 0; d < w->days; ++d) {
      DayInstance day = make_day_instance(*w, d);
      for (auto& st : day.states) {  // weekly state: exercise overtime and the weekly caps
        st.service_minutes_week = static_cast<Minutes>(rng.uniform_int(0, 3000));
        st.driving_minutes_week = static_cast<Minutes>(rng.uniform_int(0, 3200));
      }
      days.push_back(std::move(day));
    }
  }
  return days;
}

}  // namespace

TEST_CASE("fast evaluator = STN evaluator on 100 000 routes and moves", "[route_quick]") {
  const auto days = test_days();
  std::vector<RouteEvaluator> evals;
  for (const auto& d : days) evals.emplace_back(d);
  std::vector<heuristics::BaselineSequences> base;
  for (const auto& d : days) base.push_back(heuristics::territory_sequences(d));

  data::Rng rng(2026);
  std::vector<Case> cases;
  while (cases.size() < 100000) {
    const auto di = static_cast<std::size_t>(rng.uniform_int(0, static_cast<std::int64_t>(days.size()) - 1));
    const DayInstance& day = days[di];
    const auto k = static_cast<std::size_t>(rng.uniform_int(0, static_cast<std::int64_t>(day.drivers.size()) - 1));
    const auto n_orders = static_cast<std::int64_t>(day.orders.size());
    Case c{di, k, {}};
    if (cases.size() % 2 == 0) {  // random route
      const auto len = rng.uniform_int(0, 9);
      while (static_cast<std::int64_t>(c.seq.size()) < len) {
        const auto i = static_cast<std::size_t>(rng.uniform_int(0, n_orders - 1));
        if (std::ranges::find(c.seq, i) == c.seq.end()) c.seq.push_back(i);
      }
    } else {  // a move applied to a legal baseline route
      c.seq = base[di].routes[k];
      const auto move = rng.uniform_int(0, 2);
      if (move == 0 || c.seq.empty()) {  // insert
        const auto i = static_cast<std::size_t>(rng.uniform_int(0, n_orders - 1));
        if (std::ranges::find(c.seq, i) == c.seq.end()) {
          const auto pos = rng.uniform_int(0, static_cast<std::int64_t>(c.seq.size()));
          c.seq.insert(c.seq.begin() + pos, i);
        }
      } else if (move == 1) {  // remove
        c.seq.erase(c.seq.begin() + rng.uniform_int(0, static_cast<std::int64_t>(c.seq.size()) - 1));
      } else if (c.seq.size() >= 2) {  // swap
        const auto a = rng.uniform_int(0, static_cast<std::int64_t>(c.seq.size()) - 1);
        const auto b = rng.uniform_int(0, static_cast<std::int64_t>(c.seq.size()) - 1);
        std::swap(c.seq[static_cast<std::size_t>(a)], c.seq[static_cast<std::size_t>(b)]);
      }
    }
    cases.push_back(std::move(c));
  }

  int legal = 0, with_break = 0, short_break = 0, split = 0, mismatches = 0;
  for (const auto& c : cases) {
    const auto full = evals[c.day].evaluate(c.driver, c.seq);
    const auto fast = evals[c.day].quick(c.driver, c.seq);
    bool same = full.legal == fast.legal;
    if (same && full.legal) {
      // The STN route's breaks as a pattern (positions in the sequence, minutes).
      heuristics::BreakPattern pat;
      const Rules& r = days[c.day].rules;
      for (std::size_t bi = 0; bi < full.route.breaks.size(); ++bi) {
        const auto& b = full.route.breaks[bi];
        int pos = -1;
        for (std::size_t i = 0; i < c.seq.size(); ++i) {
          if (b.after_order_id == days[c.day].orders[c.seq[i]].id) pos = static_cast<int>(i);
        }
        if (bi == 0) {
          pat.i = pos;
          pat.mi = b.minutes;
        } else {
          pat.j = pos;
          pat.mj = b.minutes;
        }
      }
      pat.short_break = full.route.breaks.size() == 1 && pat.mi == r.short_break_length;
      same = full.service_minutes == fast.service_minutes && full.driving_minutes == fast.driving_minutes &&
             full.km == fast.km && full.cost == fast.cost && pat == fast.breaks;
      ++legal;
      with_break += fast.breaks.count() > 0 ? 1 : 0;
      short_break += fast.breaks.short_break ? 1 : 0;
      split += fast.breaks.count() == 2 ? 1 : 0;
    }
    if (!same && mismatches++ < 5) {
      UNSCOPED_INFO("mismatch: day " << c.day << " driver " << c.driver << " len " << c.seq.size() << " full legal "
                                     << full.legal << " theta " << full.service_minutes << " / fast legal "
                                     << fast.legal << " theta " << fast.service_minutes);
    }
  }
  WARN("cases " << cases.size() << ", legal " << legal << ", with a break " << with_break << " (short "
                << short_break << ", split " << split << ")");
  CHECK(mismatches == 0);
  CHECK(legal > 20000);
  CHECK(with_break > 2000);
  CHECK(short_break > 100);  // V1-T8 patterns exercised
  CHECK(split > 100);

  // Speed on the same cases.
  using clock = std::chrono::steady_clock;
  double sink = 0.0;
  auto t0 = clock::now();
  for (const auto& c : cases) sink += evals[c.day].evaluate(c.driver, c.seq).cost;
  const double full_s = std::chrono::duration<double>(clock::now() - t0).count();
  t0 = clock::now();
  for (const auto& c : cases) sink += evals[c.day].quick(c.driver, c.seq).cost;
  const double fast_s = std::chrono::duration<double>(clock::now() - t0).count();
  WARN("STN evaluator " << full_s * 1e6 / 1e5 << " us/route, fast " << fast_s * 1e6 / 1e5 << " us/route, speed-up "
                        << full_s / fast_s << "x (sink " << sink << ")");
#ifdef NDEBUG
  CHECK(full_s / fast_s >= 10.0);
#else
  CHECK(full_s / fast_s >= 3.0);
#endif
}
