#include "legalvrp/alns/alns.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <numeric>
#include <tuple>
#include <utility>
#include <unordered_set>

#include "legalvrp/data/rng.hpp"
#include "legalvrp/heuristics/route_eval.hpp"
#include "legalvrp/heuristics/territory.hpp"

namespace legalvrp::alns {

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kEps = 1e-9;
using Seq = std::vector<std::size_t>;

struct Solution {
  std::vector<Seq> routes;
  std::vector<double> rcost;  // route cost (quick), per driver
  std::vector<double> svc;    // today's service minutes, per driver (fairness term)
  std::vector<int> where;     // driver of each order, -1 = bank
  Seq bank;
  double cost = 0.0;
};

enum Destroy { kRandom, kWorst, kRelated, kRoute, kDestroyCount };
enum Repair { kGreedy, kRegret2, kRegret3, kRepairCount };

class Search {
 public:
  Search(const DayInstance& day, const Options& o)
      : day_(day), o_(o), ev_(day), rng_(o.seed), n_(day.orders.size()), K_(day.drivers.size()) {
    for (const auto& ord : day.orders) {
      penalty_.push_back(ord.postpone_penalty);
      const Customer& c = day.customer(ord.customer_id);
      node_.push_back(day.matrix.index_of(c.id));
      e_.push_back(c.window_start);
      q_.push_back(ord.pallets);
    }
    for (std::size_t i = 0; i < n_; ++i) {
      for (std::size_t j = 0; j < n_; ++j) max_dist_ = std::max(max_dist_, day.matrix.dist(node_[i], node_[j]));
    }
    max_dist_ = std::max(max_dist_, 1.0);
    fair_.assign(K_, false);
    w0_.assign(K_, 0.0);
    int fair_count = 0;
    for (std::size_t k = 0; k < K_; ++k) {
      fair_[k] = day.drivers[k].contract_class == o.fairness_contract;
      if (k < day.states.size()) w0_[k] = static_cast<double>(day.states[k].service_minutes_week);
      fair_count += fair_[k] ? 1 : 0;
    }
    fw_ = fair_count >= 2 ? o.fairness_weight : 0.0;
  }

  Result run();

 private:
  double route_cost(std::size_t k, const Seq& s) const {
    const auto q = ev_.quick(k, s);
    return q.legal ? q.cost : kInf;
  }
  // Route cost (kInf if illegal) and today's service minutes.
  std::pair<double, double> route_eval(std::size_t k, const Seq& s) const {
    const auto q = ev_.quick(k, s);
    return q.legal ? std::pair{q.cost, static_cast<double>(q.service_minutes)} : std::pair{kInf, 0.0};
  }
  void set_route(Solution& s, std::size_t k) const { std::tie(s.rcost[k], s.svc[k]) = route_eval(k, s.routes[k]); }
  // Fairness term: fw * (max - min) of the projected weekly service of the fairness drivers.
  // with(k, h): the term if driver k's projected service were h (O(1) from the two extremes).
  struct Spread {
    double fw = 0.0, hi1 = -kInf, hi2 = -kInf, lo1 = kInf, lo2 = kInf;
    std::size_t ihi = 0, ilo = 0;
    double value() const { return fw == 0.0 ? 0.0 : fw * (hi1 - lo1); }
    double with(std::size_t k, double h) const {
      const double hi = std::max(h, k == ihi ? hi2 : hi1);
      const double lo = std::min(h, k == ilo ? lo2 : lo1);
      return fw * (hi - lo);
    }
  };
  Spread spread(const Solution& s) const {
    Spread sp;
    sp.fw = fw_;
    if (fw_ == 0.0) return sp;
    for (std::size_t k = 0; k < K_; ++k) {
      if (!fair_[k]) continue;
      const double h = w0_[k] + s.svc[k];
      if (h > sp.hi1) {
        sp.hi2 = sp.hi1;
        sp.hi1 = h;
        sp.ihi = k;
      } else if (h > sp.hi2) {
        sp.hi2 = h;
      }
      if (h < sp.lo1) {
        sp.lo2 = sp.lo1;
        sp.lo1 = h;
        sp.ilo = k;
      } else if (h < sp.lo2) {
        sp.lo2 = h;
      }
    }
    return sp;
  }
  // Fairness change if driver k's service today becomes v (0 when off or k is outside the term).
  double fair_delta(const Spread& sp, std::size_t k, double v) const {
    return fw_ == 0.0 || !fair_[k] ? 0.0 : sp.with(k, w0_[k] + v) - sp.value();
  }
  void recompute(Solution& s) const {
    s.cost = 0.0;
    for (std::size_t k = 0; k < K_; ++k) s.cost += s.rcost[k];
    for (const auto i : s.bank) s.cost += penalty_[i];
    s.cost += spread(s).value();
  }
  Solution initial() const;
  void remove(Solution& s, std::size_t order) const;
  void destroy(Solution& s, Destroy op, int q);
  void repair(Solution& s, Repair op, bool insert_all, double noise);
  void drop_unprofitable(Solution& s) const;
  void polish(Solution& s) const;
  void add_to_pool(const Solution& s);
  std::size_t roulette(const std::vector<double>& w) { return rng_.categorical(w); }
  double elapsed() const {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0_).count();
  }

  const DayInstance& day_;
  Options o_;
  heuristics::RouteEvaluator ev_;
  data::Rng rng_;
  std::size_t n_, K_;
  std::vector<double> penalty_;
  std::vector<std::size_t> node_;
  std::vector<double> e_;
  std::vector<double> q_;
  double max_dist_ = 1.0;
  std::vector<bool> fair_;     // driver enters the fairness term
  std::vector<double> w0_;     // weekly service before today
  double fw_ = 0.0;
  std::chrono::steady_clock::time_point t0_;
  std::unordered_set<std::string> pool_keys_;
  std::vector<Column> pool_;
};

Solution Search::initial() const {
  Solution s;
  if (o_.start) {
    s.routes = *o_.start;
    s.routes.resize(K_);
  } else {
    s.routes = heuristics::territory_sequences(day_).routes;
  }
  s.where.assign(n_, -1);
  s.rcost.assign(K_, 0.0);
  s.svc.assign(K_, 0.0);
  for (std::size_t k = 0; k < K_; ++k) {
    set_route(s, k);
    if (s.rcost[k] == kInf) {  // an illegal start route: its orders go to the bank
      s.routes[k].clear();
      set_route(s, k);
    }
    for (const auto i : s.routes[k]) s.where[i] = static_cast<int>(k);
  }
  for (std::size_t i = 0; i < n_; ++i) {
    if (s.where[i] < 0) s.bank.push_back(i);
  }
  recompute(s);
  return s;
}

void Search::remove(Solution& s, std::size_t order) const {
  const int k = s.where[order];
  if (k < 0) return;
  auto& r = s.routes[static_cast<std::size_t>(k)];
  r.erase(std::ranges::find(r, order));
  s.where[order] = -1;
  s.bank.push_back(order);
}

void Search::destroy(Solution& s, Destroy op, int q) {
  std::vector<std::size_t> served;
  for (std::size_t i = 0; i < n_; ++i) {
    if (s.where[i] >= 0) served.push_back(i);
  }
  if (served.empty()) return;
  q = std::min<int>(q, static_cast<int>(served.size()));
  std::vector<bool> touched(K_, false);
  auto take = [&](std::size_t i) {
    touched[static_cast<std::size_t>(s.where[i])] = true;
    remove(s, i);
  };

  switch (op) {
    case kRandom:
      for (int t = 0; t < q; ++t) {
        const auto j = static_cast<std::size_t>(rng_.uniform_int(0, static_cast<std::int64_t>(served.size()) - 1));
        take(served[j]);
        served.erase(served.begin() + static_cast<std::ptrdiff_t>(j));
      }
      break;
    case kWorst: {
      for (int t = 0; t < q && !served.empty(); ++t) {
        std::vector<std::pair<double, std::size_t>> gain;
        for (const auto i : served) {
          const auto k = static_cast<std::size_t>(s.where[i]);
          Seq r = s.routes[k];
          r.erase(std::ranges::find(r, i));
          gain.push_back({s.rcost[k] - route_cost(k, r), i});
        }
        std::ranges::sort(gain, std::greater<>());
        const double y = rng_.uniform01();
        const auto j = static_cast<std::size_t>(y * y * y * static_cast<double>(gain.size()));
        const std::size_t pick = gain[std::min(j, gain.size() - 1)].second;
        take(pick);
        served.erase(std::ranges::find(served, pick));
      }
      break;
    }
    case kRelated: {
      const auto seed = served[static_cast<std::size_t>(rng_.uniform_int(0, static_cast<std::int64_t>(served.size()) - 1))];
      std::vector<std::size_t> removed{seed};
      take(seed);
      served.erase(std::ranges::find(served, seed));
      while (static_cast<int>(removed.size()) < q && !served.empty()) {
        const std::size_t ref = removed[static_cast<std::size_t>(rng_.uniform_int(0, static_cast<std::int64_t>(removed.size()) - 1))];
        std::vector<std::pair<double, std::size_t>> rel;
        for (const auto i : served) {
          const double d = day_.matrix.dist(node_[ref], node_[i]) / max_dist_;
          const double t = std::abs(e_[ref] - e_[i]) / 600.0;
          const double w = std::abs(q_[ref] - q_[i]) / 18.0;
          rel.push_back({9.0 * d + 3.0 * t + 2.0 * w, i});
        }
        std::ranges::sort(rel);
        const double y = rng_.uniform01();
        const double y3 = y * y * y;
        const auto j = static_cast<std::size_t>(y3 * y3 * static_cast<double>(rel.size()));
        const std::size_t pick = rel[std::min(j, rel.size() - 1)].second;
        removed.push_back(pick);
        take(pick);
        served.erase(std::ranges::find(served, pick));
      }
      break;
    }
    case kRoute: {
      std::vector<std::size_t> nonempty;
      for (std::size_t k = 0; k < K_; ++k) {
        if (!s.routes[k].empty()) nonempty.push_back(k);
      }
      int left = q;
      while (left > 0 && !nonempty.empty()) {
        const auto j = static_cast<std::size_t>(rng_.uniform_int(0, static_cast<std::int64_t>(nonempty.size()) - 1));
        const std::size_t k = nonempty[j];
        nonempty.erase(nonempty.begin() + static_cast<std::ptrdiff_t>(j));
        const Seq r = s.routes[k];
        for (const auto i : r) {
          take(i);
          --left;
        }
      }
      break;
    }
    default:
      break;
  }
  for (std::size_t k = 0; k < K_; ++k) {
    if (!touched[k]) continue;
    set_route(s, k);
    if (s.rcost[k] == kInf) {  // removal broke legality (non-triangular times): empty the route
      const Seq r = s.routes[k];
      for (const auto i : r) remove(s, i);
      set_route(s, k);
    }
  }
}

void Search::repair(Solution& s, Repair op, bool insert_all, double noise) {
  struct Best {
    double delta = kInf;  // exact cost increase
    double key = kInf;    // delta + noise (Ropke & Pisinger 2006): guides choices only
    std::size_t pos = 0;
    double svc = 0.0;     // today's service minutes of the new route (fairness)
  };
  Seq cand = s.bank;
  std::vector<std::vector<Best>> best(n_, std::vector<Best>(K_));
  Seq buf;
  auto eval_route = [&](std::size_t i, std::size_t k) {
    Best b;
    const Seq& r = s.routes[k];
    for (std::size_t pos = 0; pos <= r.size(); ++pos) {
      buf.assign(r.begin(), r.end());
      buf.insert(buf.begin() + static_cast<std::ptrdiff_t>(pos), i);
      const auto [c, v] = route_eval(k, buf);
      if (c == kInf) continue;
      const double key = (c - s.rcost[k]) + (noise > 0.0 ? noise * rng_.uniform(-1.0, 1.0) : 0.0);
      if (key < b.key) b = {c - s.rcost[k], key, pos, v};
    }
    best[i][k] = b;
  };
  for (const auto i : cand) {
    for (std::size_t k = 0; k < K_; ++k) eval_route(i, k);
  }
  const int h = op == kRegret2 ? 2 : op == kRegret3 ? 3 : 1;

  while (!cand.empty()) {
    const Spread sp = spread(s);  // fairness changes are recomputed as routes change
    auto fd = [&](std::size_t i, std::size_t k) { return fair_delta(sp, k, best[i][k].svc); };
    std::size_t pick_idx = cand.size();
    double pick_key = -kInf;
    std::vector<double> ds;
    for (std::size_t ci = 0; ci < cand.size(); ++ci) {
      const std::size_t i = cand[ci];
      ds.clear();
      double true_d1 = kInf;
      for (std::size_t k = 0; k < K_; ++k) {
        ds.push_back(best[i][k].key + fd(i, k));
        true_d1 = std::min(true_d1, best[i][k].delta + fd(i, k));
      }
      std::ranges::sort(ds);
      const double d1 = ds.empty() ? kInf : ds[0];
      if (true_d1 == kInf) continue;                                      // no legal position
      if (!insert_all && !(true_d1 < penalty_[i] - kEps)) continue;      // postponing is at least as cheap
      double key = 0.0;
      if (h == 1) {
        key = penalty_[i] - d1;  // greedy: largest saving first
      } else {
        for (int j = 1; j < h; ++j) {
          const double dj = static_cast<std::size_t>(j) < ds.size() ? std::min(ds[static_cast<std::size_t>(j)], penalty_[i]) : penalty_[i];
          key += dj - d1;
        }
        key = key * 1e6 + (penalty_[i] - d1);  // ties: larger saving
      }
      if (key > pick_key) {
        pick_key = key;
        pick_idx = ci;
      }
    }
    if (pick_idx == cand.size()) break;  // nothing profitable left: the rest stays in the bank
    const std::size_t i = cand[pick_idx];
    std::size_t k_best = 0;
    for (std::size_t k = 1; k < K_; ++k) {
      if (best[i][k].key + fd(i, k) < best[i][k_best].key + fd(i, k_best)) k_best = k;
    }
    auto& r = s.routes[k_best];
    r.insert(r.begin() + static_cast<std::ptrdiff_t>(best[i][k_best].pos), i);
    s.rcost[k_best] += best[i][k_best].delta;
    s.svc[k_best] = best[i][k_best].svc;
    s.where[i] = static_cast<int>(k_best);
    s.bank.erase(std::ranges::find(s.bank, i));
    cand.erase(cand.begin() + static_cast<std::ptrdiff_t>(pick_idx));
    for (const auto j : cand) eval_route(j, k_best);
  }
  for (std::size_t k = 0; k < K_; ++k) set_route(s, k);  // exact, no drift
  recompute(s);
}

// Orders whose removal saves more than their postponement penalty go back to the bank, best first.
// With insert-all repair this lets groups of orders that only pay off together (a remote cluster
// sharing one long drive) be built, while lone unprofitable orders are dropped.
void Search::drop_unprofitable(Solution& s) const {
  for (;;) {
    const Spread sp = spread(s);
    double best_gain = kEps;
    std::size_t best_i = n_;
    for (std::size_t i = 0; i < n_; ++i) {
      if (s.where[i] < 0) continue;
      const auto k = static_cast<std::size_t>(s.where[i]);
      Seq r = s.routes[k];
      r.erase(std::ranges::find(r, i));
      const auto [c, v] = route_eval(k, r);
      const double gain = s.rcost[k] - c - penalty_[i] - fair_delta(sp, k, v);
      if (gain > best_gain) {
        best_gain = gain;
        best_i = i;
      }
    }
    if (best_i == n_) break;
    const auto k = static_cast<std::size_t>(s.where[best_i]);
    remove(s, best_i);
    set_route(s, k);
  }
  recompute(s);
}

void Search::polish(Solution& s) const {
  for (std::size_t k = 0; k < K_; ++k) {
    Seq& r = s.routes[k];
    // A move is kept if route cost + fairness change decreases (the change is 0 when off).
    double c = 0.0, v = 0.0;
    auto better = [&](const Seq& t) {
      std::tie(c, v) = route_eval(k, t);
      const Spread sp = spread(s);
      return c + fair_delta(sp, k, v) < s.rcost[k] - kEps;
    };
    for (bool improved = true; improved;) {
      improved = false;
      for (std::size_t a = 0; a + 1 < r.size() && !improved; ++a) {  // 2-opt
        for (std::size_t b = a + 1; b < r.size() && !improved; ++b) {
          Seq t = r;
          std::reverse(t.begin() + static_cast<std::ptrdiff_t>(a), t.begin() + static_cast<std::ptrdiff_t>(b) + 1);
          if (better(t)) {
            r = std::move(t);
            s.rcost[k] = c;
            s.svc[k] = v;
            improved = true;
          }
        }
      }
      for (std::size_t a = 0; a < r.size() && !improved; ++a) {  // relocate
        for (std::size_t b = 0; b < r.size() && !improved; ++b) {
          if (a == b) continue;
          Seq t = r;
          const std::size_t u = t[a];
          t.erase(t.begin() + static_cast<std::ptrdiff_t>(a));
          t.insert(t.begin() + static_cast<std::ptrdiff_t>(b), u);
          if (better(t)) {
            r = std::move(t);
            s.rcost[k] = c;
            s.svc[k] = v;
            improved = true;
          }
        }
      }
    }
  }
  recompute(s);
}

void Search::add_to_pool(const Solution& s) {
  if (!o_.collect_pool) return;
  for (std::size_t k = 0; k < K_; ++k) {
    if (s.routes[k].empty() || pool_.size() >= o_.max_pool) continue;
    std::string key = std::to_string(k) + ":";
    for (const auto i : s.routes[k]) key += std::to_string(i) + ",";
    if (pool_keys_.insert(key).second) pool_.push_back({k, s.routes[k], s.rcost[k]});
  }
}

Result Search::run() {
  t0_ = std::chrono::steady_clock::now();
  Result res;
  res.operators = {"random", "worst", "related", "route", "greedy", "regret-2", "regret-3"};
  Solution cur = initial();
  polish(cur);
  Solution best = cur;
  add_to_pool(cur);
  res.trace.push_back({elapsed(), best.cost});

  std::vector<double> wd(kDestroyCount, 1.0), wr(kRepairCount, 1.0);
  std::vector<double> sd(kDestroyCount, 0.0), sr(kRepairCount, 0.0);
  std::vector<long long> ud(kDestroyCount, 0), ur(kRepairCount, 0), seg_ud(kDestroyCount, 0), seg_ur(kRepairCount, 0);
  const double T0 = std::max(1e-6, o_.start_worse_share * std::max(cur.cost, 1.0) / data::portable_log(2.0));
  const double Tend = T0 * o_.end_temperature_ratio;

  long long it = 0;
  for (;; ++it) {
    if (o_.max_iterations > 0 && it >= o_.max_iterations) break;
    const double t = elapsed();
    if (t >= o_.time_limit_s) break;
    const double progress = o_.max_iterations > 0 ? static_cast<double>(it) / static_cast<double>(o_.max_iterations)
                                                  : t / o_.time_limit_s;
    const double T = T0 * data::portable_exp(progress * data::portable_log(Tend / T0));

    // Destroy size relative to all orders, at least min(n, 4) at the top end (small days need
    // multi-order moves: e.g. swapping three orders between two drivers).
    const auto all = static_cast<int>(n_);
    const int q_min = std::max(1, static_cast<int>(std::lround(o_.min_remove_share * all)));
    const int q_max = std::max({q_min, std::min(all, 4),
                                std::min(o_.max_remove, static_cast<int>(std::lround(o_.max_remove_share * all)))});
    const int q = static_cast<int>(rng_.uniform_int(q_min, q_max));

    const auto d = static_cast<Destroy>(roulette(wd));
    const auto r = static_cast<Repair>(roulette(wr));
    Solution cand = cur;
    destroy(cand, d, q);
    const bool insert_all = rng_.bernoulli(0.5);
    const double noise = rng_.bernoulli(0.5) ? 0.1 * cur.cost / static_cast<double>(std::max<std::size_t>(n_, 1)) : 0.0;
    repair(cand, r, insert_all, noise);
    if (insert_all) drop_unprofitable(cand);
    const auto di = static_cast<std::size_t>(d);
    const auto ri = static_cast<std::size_t>(r);
    ++ud[di];
    ++ur[ri];
    ++seg_ud[di];
    ++seg_ur[ri];

    double score = 0.0;
    if (cand.cost < best.cost - kEps) {
      polish(cand);
      best = cand;
      cur = cand;
      score = 33.0;
      res.trace.push_back({elapsed(), best.cost});
      add_to_pool(cur);
    } else if (cand.cost < cur.cost - kEps) {
      cur = std::move(cand);
      score = 9.0;
      add_to_pool(cur);
    } else if (rng_.uniform01() < data::portable_exp(-(cand.cost - cur.cost) / T)) {
      cur = std::move(cand);
      score = 13.0;
      add_to_pool(cur);
    }
    sd[di] += score;
    sr[ri] += score;
    if ((it + 1) % o_.segment == 0) {  // adaptive weights
      for (std::size_t j = 0; j < static_cast<std::size_t>(kDestroyCount); ++j) {
        if (seg_ud[j] > 0) wd[j] = wd[j] * (1 - o_.reaction) + o_.reaction * sd[j] / static_cast<double>(seg_ud[j]);
        wd[j] = std::max(wd[j], 0.05);
        sd[j] = 0.0;
        seg_ud[j] = 0;
      }
      for (std::size_t j = 0; j < static_cast<std::size_t>(kRepairCount); ++j) {
        if (seg_ur[j] > 0) wr[j] = wr[j] * (1 - o_.reaction) + o_.reaction * sr[j] / static_cast<double>(seg_ur[j]);
        wr[j] = std::max(wr[j], 0.05);
        sr[j] = 0.0;
        seg_ur[j] = 0;
      }
    }
  }

  res.routes = best.routes;
  res.bank = best.bank;
  std::ranges::sort(res.bank);
  res.cost = best.cost;
  res.iterations = it;
  res.runtime_s = elapsed();
  res.plan = make_plan(day_, res.routes, res.bank);
  res.plan.solver_stats.status = "ALNS";
  res.plan.solver_stats.runtime_s = res.runtime_s;
  res.plan.solver_stats.objective = res.cost;
  res.pool = std::move(pool_);
  res.weights.insert(res.weights.end(), wd.begin(), wd.end());
  res.weights.insert(res.weights.end(), wr.begin(), wr.end());
  res.uses.insert(res.uses.end(), ud.begin(), ud.end());
  res.uses.insert(res.uses.end(), ur.begin(), ur.end());
  return res;
}

}  // namespace

DayPlan make_plan(const DayInstance& day, const std::vector<std::vector<std::size_t>>& routes,
                  const std::vector<std::size_t>& bank) {
  const heuristics::RouteEvaluator ev(day);
  DayPlan plan;
  plan.day = day.day;
  for (std::size_t k = 0; k < day.drivers.size(); ++k) {
    const auto e = ev.evaluate(k, k < routes.size() ? routes[k] : Seq{});
    plan.objective += e.cost;
    if (k < routes.size() && !routes[k].empty()) plan.routes.push_back(e.route);
  }
  for (const auto i : bank) {
    plan.postponed_order_ids.push_back(day.orders[i].id);
    plan.objective += day.orders[i].postpone_penalty;
  }
  return plan;
}

Result solve(const DayInstance& day, const Options& options) {
  if (day.drivers.empty()) {
    Result r;
    for (std::size_t i = 0; i < day.orders.size(); ++i) r.bank.push_back(i);
    r.plan = make_plan(day, {}, r.bank);
    r.cost = r.plan.objective;
    return r;
  }
  Search s(day, options);
  return s.run();
}

}  // namespace legalvrp::alns
