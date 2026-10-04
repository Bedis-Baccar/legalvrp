#include "legalvrp/cg/colgen.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>
#include <tuple>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

#include "legalvrp/heuristics/route_eval.hpp"

namespace legalvrp::cg {

namespace {

constexpr double kEps = 1e-6;
using Seq = std::vector<std::size_t>;

}  // namespace

Result solve(GRBEnv& env, const DayInstance& day, const Options& o) {
  const auto t0 = std::chrono::steady_clock::now();
  auto elapsed = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };
  const std::size_t n = day.orders.size();
  const std::size_t K = day.drivers.size();
  const heuristics::RouteEvaluator ev(day);
  Result res;

  std::vector<double> idle(K);
  double constant = 0.0;
  for (std::size_t k = 0; k < K; ++k) constant += idle[k] = ev.quick(k, {}).cost;

  GRBModel m(env);
  m.set(GRB_IntParam_OutputFlag, 0);
  m.set(GRB_IntParam_Threads, o.threads);
  std::vector<GRBVar> u(n);
  std::vector<GRBConstr> cover(n), one(K);
  for (std::size_t i = 0; i < n; ++i) {
    u[i] = m.addVar(0, GRB_INFINITY, day.orders[i].postpone_penalty, GRB_CONTINUOUS, "u" + std::to_string(i));
  }
  m.update();
  for (std::size_t i = 0; i < n; ++i) cover[i] = m.addConstr(GRBLinExpr(u[i]) == 1.0, "cover" + std::to_string(i));
  for (std::size_t k = 0; k < K; ++k) one[k] = m.addConstr(GRBLinExpr(0.0) <= 1.0, "one" + std::to_string(k));
  m.update();

  std::map<std::pair<std::size_t, Seq>, GRBVar> cols;
  auto add_column = [&](std::size_t k, const Seq& seq, double cost) {
    if (seq.empty() || cols.contains({k, seq})) return false;
    Seq sorted = seq;  // columns are elementary routes
    std::ranges::sort(sorted);
    if (std::ranges::adjacent_find(sorted) != sorted.end()) {
      std::string s;
      for (const auto i : seq) s += std::to_string(i) + " ";
      throw std::logic_error("cg: non-elementary column for driver " + std::to_string(k) + ": " + s);
    }
    GRBColumn col;
    for (const auto i : seq) col.addTerm(1.0, cover[i]);
    col.addTerm(1.0, one[k]);
    cols.emplace(std::make_pair(k, seq), m.addVar(0, GRB_INFINITY, cost - idle[k], GRB_CONTINUOUS, col));
    return true;
  };
  for (const auto& c : o.initial) {
    if (c.driver < K) add_column(c.driver, c.seq, c.cost);
  }
  for (std::size_t k = 0; k < K && k < o.incumbent.size(); ++k) {
    const auto q = ev.quick(k, o.incumbent[k]);
    if (q.legal) add_column(k, o.incumbent[k], q.cost);
  }
  // Upper bound of the incumbent (exact costs).
  double incumbent_cost = constant;
  {
    std::vector<bool> served(n, false);
    for (std::size_t k = 0; k < K && k < o.incumbent.size(); ++k) {
      if (o.incumbent[k].empty()) continue;
      incumbent_cost += ev.quick(k, o.incumbent[k]).cost - idle[k];
      for (const auto i : o.incumbent[k]) served[i] = true;
    }
    for (std::size_t i = 0; i < n; ++i) {
      if (!served[i]) incumbent_cost += day.orders[i].postpone_penalty;
    }
  }
  res.upper_bound = o.incumbent.empty() ? std::numeric_limits<double>::infinity() : incumbent_cost;
  res.routes = o.incumbent;
  res.routes.resize(K);
  {
    std::vector<bool> served(n, false);
    for (const auto& r : res.routes) {
      for (const auto i : r) served[i] = true;
    }
    for (std::size_t i = 0; i < n; ++i) {
      if (!served[i]) res.bank.push_back(i);
    }
  }

  // Identical drivers (contract, shift, truck, weekly state) share one pricing problem: the
  // V1-T4 instances cycle driver templates. The bound counts a group's minimum once per driver.
  std::vector<std::vector<std::size_t>> groups;
  {
    std::map<std::tuple<std::string, Minutes, Minutes, Pallets, bool, Minutes, Minutes>, std::size_t> by_signature;
    for (std::size_t k = 0; k < K; ++k) {
      const Driver& d = day.drivers[k];
      const Truck& t = day.truck(d.truck_id);
      const DriverWeekState& st = day.state(d.id);
      const auto key = std::make_tuple(d.contract_class, d.shift_start, d.shift_end_max, t.capacity_pallets,
                                       t.has_tail_lift, st.service_minutes_week, st.driving_minutes_week);
      const auto [it, fresh] = by_signature.emplace(key, groups.size());
      if (fresh) groups.emplace_back();
      groups[it->second].push_back(k);
    }
  }
  std::vector<RelaxedPricer> pricers;
  for (const auto& g : groups) {
    pricers.emplace_back(day, g.front());
    pricers.back().reset_ng(o.pricing.ng);
  }
  res.pricing_groups = static_cast<int>(groups.size());
  double best_lb = -std::numeric_limits<double>::infinity();
  int ng_needed = o.pricing.ng;

  for (;;) {
    bool bound_done = false;  // the bound of this iteration's duals was computed
    m.optimize();
    if (m.get(GRB_IntAttr_Status) != GRB_OPTIMAL) break;
    ++res.iterations;
    res.lp_value = m.get(GRB_DoubleAttr_ObjVal) + constant;
    std::vector<double> pi(n), mu(K);
    for (std::size_t i = 0; i < n; ++i) pi[i] = cover[i].get(GRB_DoubleAttr_Pi);
    for (std::size_t k = 0; k < K; ++k) mu[k] = one[k].get(GRB_DoubleAttr_Pi);
    // Pricing. A truncated labelling (few labels) proposes columns quickly; the exact one, which
    // gives the bound, runs when the truncated runs and the insertions find nothing.
    std::vector<std::pair<double, std::pair<std::size_t, Seq>>> candidates;  // (reduced cost, column)
    auto consider = [&](std::size_t k, const Seq& seq) {
      Seq sorted = seq;  // ng-routes may revisit an order: only elementary routes are columns
      std::ranges::sort(sorted);
      if (std::ranges::adjacent_find(sorted) != sorted.end()) return false;
      const auto q = ev.quick(k, seq);
      if (!q.legal) return false;
      double rc = q.cost - idle[k] - mu[k];
      for (const auto i : seq) rc -= pi[i];
      if (rc < -kEps) candidates.push_back({rc, {k, seq}});
      return true;
    };
    auto take = [&](std::size_t k, const PricingResult& pr) {
      for (const auto& rr : pr.best) {
        if (consider(k, rr.seq)) continue;
        for (std::size_t drop = 0; drop < rr.seq.size(); ++drop) {  // repair: drop one stop
          Seq s = rr.seq;
          s.erase(s.begin() + static_cast<std::ptrdiff_t>(drop));
          if (!s.empty()) consider(k, s);
        }
      }
    };
    double sum_pi = 0.0;
    for (const double p : pi) sum_pi += p;
    // The bound of these duals: exact pricing for every driver (smaller ng if a run is too large).
    auto exact_bound = [&](bool collect, std::chrono::steady_clock::time_point deadline) {
      double lb = constant + sum_pi;
      int ng_iter = 0;
      for (std::size_t g = 0; g < groups.size(); ++g) {
        RelaxedPricer& pricer = pricers[g];
        PricingOptions po = o.pricing;
        po.deadline = deadline;
        PricingResult pr = pricer.price(pi, po);
        // Dynamic ng: forbid the cycles of the best relaxed routes and price again (tighter).
        for (int round = 0; pr.exact && round < o.ng_rounds; ++round) {
          const auto saved = pricer.ng_sets();
          bool grew = false;
          for (std::size_t t = 0; t < pr.best.size() && t < 5; ++t) grew = pricer.forbid_cycles(pr.best[t].seq) || grew;
          if (!grew) break;  // the best relaxed routes are elementary
          PricingResult again = pricer.price(pi, po);
          if (!again.exact) {  // too many labels: keep the smaller neighbourhoods
            pricer.set_ng_sets(saved);
            break;
          }
          pr = std::move(again);
        }
        for (int size = o.pricing.ng; !pr.exact && size > 1 && std::chrono::steady_clock::now() < deadline;) {  // weaker, fewer labels
          size = std::max(1, size / 2);
          pricer.reset_ng(size);
          pr = pricer.price(pi, po);
        }
        if (!pr.exact) return false;
        ng_iter = std::max(ng_iter, pricer.ng_max());
        lb += pr.min_value * static_cast<double>(groups[g].size());
        if (collect) {
          for (const auto k : groups[g]) take(k, pr);
        }
      }
      if (lb > best_lb) {
        best_lb = lb;
        ng_needed = ng_iter;
      }
      return true;
    };
    {
      double lb = constant + sum_pi;
      bool all_exact = true;
      for (std::size_t g = 0; g < groups.size(); ++g) {
        PricingOptions po = o.pricing;
        po.max_labels = std::min(po.max_labels, o.heuristic_labels);
        const PricingResult pr = pricers[g].price(pi, po);
        for (const auto k : groups[g]) take(k, pr);
        all_exact = all_exact && pr.exact;
        lb += pr.min_value * static_cast<double>(groups[g].size());
      }
      if (all_exact && lb > best_lb) {  // small instances: the truncated runs were complete
        best_lb = lb;
        ng_needed = o.pricing.ng;
      }
      bound_done = all_exact;
    }

    // Insertions into the routes of the LP solution.
    for (const auto& [key, var] : cols) {
      if (var.get(GRB_DoubleAttr_X) < 1e-6) continue;
      const auto& [k, seq] = key;
      for (std::size_t i = 0; i < n; ++i) {
        if (std::ranges::find(seq, i) != seq.end()) continue;
        double best_rc = 0.0;
        Seq best_seq;
        for (std::size_t pos = 0; pos <= seq.size(); ++pos) {
          Seq s = seq;
          s.insert(s.begin() + static_cast<std::ptrdiff_t>(pos), i);
          const auto q = ev.quick(k, s);
          if (!q.legal) continue;
          double rc = q.cost - idle[k] - mu[k];
          for (const auto x : s) rc -= pi[x];
          if (rc < best_rc - kEps) {
            best_rc = rc;
            best_seq = std::move(s);
          }
        }
        if (!best_seq.empty()) candidates.push_back({best_rc, {k, best_seq}});
      }
    }

    // Nothing new from the cheap pricing: the exact one (bound, and its own candidates).
    auto count_new = [&] {
      int fresh = 0;
      for (const auto& c : candidates) fresh += cols.contains(c.second) ? 0 : 1;
      return fresh;
    };
    using Clock = std::chrono::steady_clock;
    const auto cg_deadline = t0 + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(o.time_limit_s));
    if (!bound_done && count_new() == 0) bound_done = exact_bound(true, cg_deadline);
    res.trace.push_back({elapsed(), res.lp_value, best_lb});

    std::ranges::sort(candidates, {}, &std::pair<double, std::pair<std::size_t, Seq>>::first);
    int added = 0;
    for (const auto& [rc, col] : candidates) {
      if (added >= o.max_new_columns) break;
      const auto q = ev.quick(col.first, col.second);
      if (add_column(col.first, col.second, q.cost)) ++added;
    }
    if (added == 0 && bound_done) res.converged = true;  // no column even from the exact pricing
    if (added == 0 || best_lb >= res.lp_value - 1e-6 || elapsed() >= o.time_limit_s) {
      if (!bound_done) {  // the bound of these duals (valid for any duals), on its own budget
        exact_bound(false, Clock::now() + std::chrono::duration_cast<Clock::duration>(
                                              std::chrono::duration<double>(o.bound_time_s)));
      }
      break;
    }
    m.update();
  }
  res.columns = cols.size();
  res.lower_bound = best_lb;
  res.bound_valid = best_lb > -std::numeric_limits<double>::infinity();
  res.ng_used = ng_needed;

  // Upper bound: the master over the generated columns as a MIP.
  if (o.mip_time_s > 0) {
    for (auto& [key, var] : cols) var.set(GRB_CharAttr_VType, GRB_BINARY);
    for (auto& v : u) {
      v.set(GRB_CharAttr_VType, GRB_BINARY);
      v.set(GRB_DoubleAttr_UB, 1.0);
    }
    m.set(GRB_DoubleParam_TimeLimit, o.mip_time_s);
    m.set(GRB_DoubleParam_MIPGap, 0.0);
    for (auto& [key, var] : cols) {
      const bool on = key.first < o.incumbent.size() && o.incumbent[key.first] == key.second;
      var.set(GRB_DoubleAttr_Start, on ? 1.0 : 0.0);
    }
    m.optimize();
    if (m.get(GRB_IntAttr_SolCount) > 0) {
      const double ub = m.get(GRB_DoubleAttr_ObjVal) + constant;
      if (ub < res.upper_bound - 1e-6) {
        res.upper_bound = ub;
        res.routes.assign(K, {});
        for (const auto& [key, var] : cols) {
          if (var.get(GRB_DoubleAttr_X) > 0.5) res.routes[key.first] = key.second;
        }
        res.bank.clear();
        for (std::size_t i = 0; i < n; ++i) {
          if (u[i].get(GRB_DoubleAttr_X) > 0.5) res.bank.push_back(i);
        }
      }
    }
  }
  res.runtime_s = elapsed();
  return res;
}

}  // namespace legalvrp::cg
