#include "legalvrp/cg/pricing.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <functional>
#include <limits>
#include <queue>
#include <stdexcept>

namespace legalvrp::cg {

namespace {


using Mem = NgSet;

bool has(const Mem& m, std::size_t i) { return ((m[i >> 6] >> (i & 63)) & 1U) != 0; }
void set(Mem& m, std::size_t i) { m[i >> 6] |= (std::uint64_t{1} << (i & 63)); }
bool subset(const Mem& a, const Mem& b) {  // a within b
  for (std::size_t w = 0; w < a.size(); ++w) {
    if ((a[w] & ~b[w]) != 0) return false;
  }
  return true;
}

// Break pattern so far (the planners' patterns: none, full, short, 15 then 30).
enum State : std::uint8_t { kNone, kFull, kShort, kPart, kSplit };

struct Label {
  std::size_t node;      // order index
  int parent;            // label index, -1: from the depot
  double cost;           // km cost - duals so far
  long long ready;       // earliest time ready to leave the node (its service, and its break if taken)
  long long load, drive, drive_q;  // drive_q: driving since the last qualifying break
  long long ssl;         // latest possible start of the current work stretch (S, or a break end)
  std::uint8_t state;
  bool broke_here;       // a break follows this node's service
  Mem mem;
  bool alive;
};

}  // namespace

RelaxedPricer::RelaxedPricer(const DayInstance& day, std::size_t k)
    : day_(day), k_(k), n_(day.orders.size()), rules_(day.rules) {
  if (n_ > kMaxPricingOrders) throw std::invalid_argument("cg pricing: more than 256 orders");
  const Driver& drv = day.drivers[k];
  const Truck& truck = day.truck(drv.truck_id);
  const Contract& con = day.contract(drv.contract_class);
  const DriverWeekState& st = day.state(drv.id);
  const Rules& r = day.rules;
  depot_ = day.matrix.index_of(day.depot.id);
  for (const auto& o : day.orders) {
    const Customer& c = day.customer(o.customer_id);
    node_.push_back(day.matrix.index_of(c.id));
    e_.push_back(c.window_start);
    l_.push_back(c.window_end);
    s_.push_back(o.service_mu);
    q_.push_back(o.pallets);
    compat_.push_back((!c.needs_tail_lift || truck.has_tail_lift) && o.pallets <= truck.capacity_pallets);
  }
  S_ = drv.shift_start;
  F_ = drv.shift_end_max;
  P_ = r.depot_prep;
  R_ = r.depot_close;
  DB_ = r.drive_before_break;
  Dmax_ = std::min<long long>(r.daily_drive_max, r.weekly_drive_max - st.driving_minutes_week);
  Wmax_ = std::min<long long>(r.daily_service_max, con.weekly_service_max - st.service_minutes_week);
  WB_ = r.work_before_break;
  Q_ = truck.capacity_pallets;
  c_km_ = day.costs.cost_per_km;
  c_reg_ = con.cost_per_min_regular;
  c_ext_ = con.cost_per_min_extra;
  c_fix_ = con.fixed_cost_if_used;
  W0_ = st.service_minutes_week;
  thr_ = con.weekly_threshold;
  idle_ = c_ext_ * std::max(0.0, W0_ - thr_);

  // Shortest travel time from each order to the depot (Floyd-Warshall over orders + depot).
  const std::size_t N = n_ + 1;  // index n_ = depot
  auto mnode = [&](std::size_t a) { return a == n_ ? depot_ : node_[a]; };
  std::vector<long long> d(N * N);
  for (std::size_t a = 0; a < N; ++a) {
    for (std::size_t b = 0; b < N; ++b) d[a * N + b] = a == b ? 0 : day.matrix.time(mnode(a), mnode(b));
  }
  for (std::size_t m = 0; m < N; ++m) {
    for (std::size_t a = 0; a < N; ++a) {
      for (std::size_t b = 0; b < N; ++b) d[a * N + b] = std::min(d[a * N + b], d[a * N + m] + d[m * N + b]);
    }
  }
  sp_back_.resize(n_);
  for (std::size_t i = 0; i < n_; ++i) sp_back_[i] = d[i * N + n_];
  ng_order_.resize(n_);
  for (std::size_t i = 0; i < n_; ++i) {
    for (std::size_t j = 0; j < n_; ++j) {
      if (j != i) ng_order_[i].push_back(j);
    }
    std::ranges::stable_sort(ng_order_[i], {}, [&](std::size_t j) { return day.matrix.time(node_[i], node_[j]); });
  }
  reset_ng(PricingOptions{}.ng);
}

void RelaxedPricer::reset_ng(int ng) {
  neigh_.assign(n_, Mem{});
  for (std::size_t i = 0; i < n_; ++i) {
    set(neigh_[i], i);
    for (std::size_t t = 0; t + 1 < static_cast<std::size_t>(std::max(1, ng)) && t < ng_order_[i].size(); ++t) {
      set(neigh_[i], ng_order_[i][t]);
    }
  }
}

bool RelaxedPricer::forbid_cycles(const std::vector<std::size_t>& seq) {
  // A cycle i ... i: remember i at every stop in between, so that the second visit is forbidden.
  bool grew = false;
  for (std::size_t p = 0; p < seq.size(); ++p) {
    for (std::size_t q = p + 1; q < seq.size(); ++q) {
      if (seq[q] != seq[p]) continue;
      for (std::size_t t = p + 1; t < q; ++t) {
        if (!has(neigh_[seq[t]], seq[p])) {
          set(neigh_[seq[t]], seq[p]);
          grew = true;
        }
      }
      break;
    }
  }
  return grew;
}

int RelaxedPricer::ng_max() const {
  int best = 0;
  for (const auto& s : neigh_) {
    int c = 0;
    for (const auto w : s) c += std::popcount(w);
    best = std::max(best, c);
  }
  return best;
}

double RelaxedPricer::work_cost(double theta) const {
  return c_reg_ * theta + c_ext_ * std::max(0.0, W0_ + theta - thr_);
}

namespace {

// Minutes of break taken in a state, and the breaks the planners may still take from it.
long long state_minutes(std::uint8_t s, const Rules& r) {
  switch (s) {
    case kFull: return r.break_length;
    case kShort: return r.short_break_length;
    case kPart: return r.split_break_first;
    case kSplit: return r.split_break_first + r.split_break_second;
    default: return 0;
  }
}
struct Transition {
  std::uint8_t to;
  long long minutes;
  bool qualifying;  // restarts the driving count
};
std::vector<Transition> transitions(std::uint8_t s, const Rules& r) {
  std::vector<Transition> t;
  if (s == kNone) {
    t.push_back({kFull, r.break_length, true});
    if (r.allow_short_break) t.push_back({kShort, r.short_break_length, false});
    if (r.allow_split_break) t.push_back({kPart, r.split_break_first, false});
  } else if (s == kPart) {
    t.push_back({kSplit, r.split_break_second, true});
  }
  return t;
}

}  // namespace

// The relaxed rules of one step, shared by the labelling and relaxed_cost() so that both
// define exactly the same set of relaxed routes.
namespace {
struct Step {
  long long ready, load, drive, drive_q, ssl;
  std::uint8_t state;
  bool broke_here;
};
}  // namespace

double RelaxedPricer::relaxed_cost(const std::vector<std::size_t>& seq) const {
  // Minimum over the planners' break patterns (none, full at b, short at b, split at i < j).
  constexpr double kInf = std::numeric_limits<double>::infinity();
  if (seq.empty()) return 0.0;
  const Matrix& m = day_.matrix;
  const std::size_t n = seq.size();
  std::vector<std::vector<std::pair<std::size_t, Transition>>> options{{}};  // breaks: (position, transition)
  for (std::size_t b = 0; b < n; ++b) {
    for (const auto& t : transitions(kNone, rules_)) {
      if (t.to == kPart) continue;
      options.push_back({{b, t}});
    }
  }
  if (rules_.allow_split_break) {
    const auto first = transitions(kNone, rules_).back();  // kPart
    const auto second = transitions(kPart, rules_).front();
    for (std::size_t i = 0; i + 1 < n; ++i) {
      for (std::size_t j = i + 1; j < n; ++j) options.push_back({{i, first}, {j, second}});
    }
  }
  double best = kInf;
  for (const auto& opt : options) {
    Step st{S_ + P_, 0, 0, 0, S_, kNone, false};
    double km = 0.0;
    std::size_t from = depot_;
    bool ok = true;
    for (std::size_t pos = 0; pos < n && ok; ++pos) {
      const std::size_t j = seq[pos];
      if (!compat_[j]) {
        ok = false;
        break;
      }
      const long long tau = m.time(from, node_[j]);
      const long long arrival = st.ready + tau;
      st.drive_q += tau;
      st.drive += tau;
      if (st.broke_here) st.ssl = std::min(st.ssl, l_[j] - tau);
      st.broke_here = false;
      if (st.drive_q > DB_ || arrival > l_[j]) {
        ok = false;
        break;
      }
      st.ready = std::max(e_[j], arrival) + s_[j];
      st.load += q_[j];
      km += m.dist(from, node_[j]);
      from = node_[j];
      if (st.ready - st.ssl > WB_) {
        ok = false;
        break;
      }
      for (const auto& [p, t] : opt) {
        if (p != pos) continue;
        const long long latest = std::min(st.ssl + WB_, l_[j] + s_[j]);  // latest break start
        st.ready += t.minutes;
        st.ssl = latest + t.minutes;
        st.state = t.to;
        st.broke_here = true;
        if (t.qualifying) st.drive_q = 0;
      }
    }
    if (!ok || st.load > Q_ || st.state == kPart) continue;
    const long long tau = m.time(from, depot_);
    st.drive_q += tau;
    st.drive += tau;
    const long long tE = st.ready + tau;
    if (st.broke_here) st.ssl = std::min(st.ssl, F_ - R_ - tau);
    const long long theta = tE + R_ - S_ - state_minutes(st.state, rules_);
    if (st.drive_q > DB_ || st.drive > Dmax_ || tE + R_ > F_ || tE + R_ - st.ssl > WB_ || theta > Wmax_ ||
        (st.state == kShort && theta > rules_.short_break_work_max)) {
      continue;
    }
    km += m.dist(from, depot_);
    best = std::min(best, c_km_ * km + work_cost(static_cast<double>(theta)) + c_fix_ - idle_);
  }
  return best;
}

PricingResult RelaxedPricer::price(const std::vector<double>& pi, const PricingOptions& o) const {
  PricingResult res;
  const Matrix& m = day_.matrix;
  const std::vector<Mem>& neigh = neigh_;

  std::vector<Label> labels;
  std::vector<std::vector<int>> at(n_ * 10);  // labels per (stop, pattern state, break here): dominance buckets
  using Item = std::pair<long long, int>;  // (ready, label)
  std::priority_queue<Item, std::vector<Item>, std::greater<>> queue;
  std::vector<std::pair<double, int>> done;  // (value, label) of negative completions

  auto dominates = [](const Label& a, const Label& b) {
    return a.state == b.state && a.broke_here == b.broke_here && a.cost <= b.cost + 1e-9 && a.ready <= b.ready &&
           a.load <= b.load && a.drive <= b.drive && a.drive_q <= b.drive_q && a.ssl >= b.ssl && subset(a.mem, b.mem);
  };
  auto insert = [&](const Label& nl) {
    const std::size_t j = nl.node * 10 + static_cast<std::size_t>(nl.state) * 2 + (nl.broke_here ? 1U : 0U);
    for (const int e : at[j]) {
      const Label& old = labels[static_cast<std::size_t>(e)];
      if (old.alive && dominates(old, nl)) return false;
    }
    for (const int e : at[j]) {
      Label& old = labels[static_cast<std::size_t>(e)];
      if (old.alive && dominates(nl, old)) old.alive = false;
    }
    std::erase_if(at[j], [&](int e) { return !labels[static_cast<std::size_t>(e)].alive; });
    labels.push_back(nl);
    const int idx = static_cast<int>(labels.size()) - 1;
    at[j].push_back(idx);
    queue.push({nl.ready, idx});
    return true;
  };
  // Completion bound (prunes labels that cannot beat the best route found so far, which keeps the
  // minimum exact): from ready time t, further stops earn at most `ratio` per minute of the
  // remaining shift, ratio = max over stops j of (pi_j - c_km * shortest km into j) / (s_j +
  // shortest time into j) (revisits included); the return costs >= 0 km; theta >= t + R - S - the
  // longest break total, and the work cost is non-decreasing.
  double ratio = 0.0, ratio_q = 0.0;  // best gain per minute, per pallet
  for (std::size_t j = 0; j < n_; ++j) {
    if (!compat_[j]) continue;
    long long tmin = m.time(depot_, node_[j]);
    double kmin = m.dist(depot_, node_[j]);
    for (std::size_t i = 0; i < n_; ++i) {
      if (i == j || !compat_[i]) continue;
      tmin = std::min<long long>(tmin, m.time(node_[i], node_[j]));
      kmin = std::min(kmin, m.dist(node_[i], node_[j]));
    }
    const double v = pi[j] - c_km_ * kmin;
    if (v > 0.0) {
      ratio = std::max(ratio, v / static_cast<double>(std::max<long long>(1, s_[j] + tmin)));
      ratio_q = std::max(ratio_q, v / static_cast<double>(std::max<long long>(1, q_[j])));
    }
  }
  const long long breaks_max = std::max<long long>(
      {static_cast<long long>(rules_.break_length), state_minutes(kShort, rules_), state_minutes(kSplit, rules_)});
  auto promising = [&](const Label& L) {
    // Further stops use both shift time and pallets: the smaller of the two gains bounds them.
    const double rest = std::min(ratio * static_cast<double>(std::max<long long>(0, F_ - R_ - L.ready)),
                                 ratio_q * static_cast<double>(std::max<long long>(0, Q_ - L.load)));
    const double theta = static_cast<double>(std::max<long long>(0, L.ready + R_ - S_ - breaks_max));
    return L.cost - rest + work_cost(theta) + c_fix_ - idle_ < res.min_value - 1e-9;
  };
  // Necessary conditions for some completion of a label (every completion returns through at
  // least the shortest path to the depot; with no break left, the stretch and the driving go on).
  auto completable = [&](const Label& L) {
    if (!promising(L)) return false;
    const long long back = sp_back_[L.node];
    if (L.drive + back > Dmax_ || L.ready + back + R_ > F_) return false;
    const bool more_breaks = L.state == kNone || L.state == kPart;
    if (!more_breaks && (L.drive_q + back > DB_ || L.ready + back + R_ - L.ssl > WB_)) return false;
    return L.ready + back + R_ - S_ - state_minutes(L.state, rules_) <= Wmax_;
  };
  auto extend = [&](int parent, std::size_t j) {
    if (!compat_[j]) return;
    const Label* p = parent >= 0 ? &labels[static_cast<std::size_t>(parent)] : nullptr;
    if (p != nullptr && has(p->mem, j)) return;
    const std::size_t from = p != nullptr ? node_[p->node] : depot_;
    const long long tau = m.time(from, node_[j]);
    Label nl;
    nl.node = j;
    nl.parent = parent;
    nl.drive_q = (p != nullptr ? p->drive_q : 0) + tau;
    nl.drive = (p != nullptr ? p->drive : 0) + tau;
    const long long arrival = (p != nullptr ? p->ready : S_ + P_) + tau;
    if (nl.drive_q > DB_ || arrival > l_[j]) return;
    nl.ssl = p != nullptr ? p->ssl : S_;
    if (p != nullptr && p->broke_here) nl.ssl = std::min(nl.ssl, l_[j] - tau);  // the break ended in time for j
    nl.ready = std::max(e_[j], arrival) + s_[j];
    nl.load = (p != nullptr ? p->load : 0) + q_[j];
    if (nl.load > Q_ || nl.ready - nl.ssl > WB_) return;
    nl.state = p != nullptr ? p->state : kNone;
    nl.broke_here = false;
    nl.mem = Mem{};
    if (p != nullptr) {
      for (std::size_t w = 0; w < nl.mem.size(); ++w) nl.mem[w] = p->mem[w] & neigh[j][w];
    }
    set(nl.mem, j);
    nl.cost = (p != nullptr ? p->cost : 0.0) + c_km_ * m.dist(from, node_[j]) - pi[j];
    nl.alive = true;
    if (!completable(nl)) return;
    insert(nl);
    // Breaks after this service: each allowed transition, starting as early as possible; the
    // stretch after it starts at the latest possible break end (D-007: waiting may delay it).
    for (const auto& t : transitions(nl.state, rules_)) {
      Label bl = nl;
      const long long latest = std::min(nl.ssl + WB_, l_[j] + s_[j]);
      bl.ready = nl.ready + t.minutes;
      bl.ssl = latest + t.minutes;
      bl.state = t.to;
      bl.broke_here = true;
      if (t.qualifying) bl.drive_q = 0;
      if (completable(bl)) insert(bl);
    }
  };

  for (std::size_t j = 0; j < n_; ++j) extend(-1, j);
  res.exact = true;
  std::size_t pops = 0;
  while (!queue.empty()) {
    if (labels.size() > o.max_labels || (++pops % 4096 == 0 && std::chrono::steady_clock::now() > o.deadline)) {
      res.exact = false;
      break;
    }
    const int idx = queue.top().second;
    queue.pop();
    if (!labels[static_cast<std::size_t>(idx)].alive) continue;
    const Label L = labels[static_cast<std::size_t>(idx)];
    if (!promising(L)) continue;  // the best so far improved since it was created
    // Return to the depot (no break pattern may end on its first part alone).
    if (L.state != kPart) {
      const long long tau = m.time(node_[L.node], depot_);
      const long long drive_q = L.drive_q + tau, drive = L.drive + tau, tE = L.ready + tau;
      const long long ssl = L.broke_here ? std::min(L.ssl, F_ - R_ - tau) : L.ssl;
      const long long theta = tE + R_ - S_ - state_minutes(L.state, rules_);
      if (drive_q <= DB_ && drive <= Dmax_ && tE + R_ <= F_ && tE + R_ - ssl <= WB_ && theta <= Wmax_ &&
          (L.state != kShort || theta <= rules_.short_break_work_max)) {
        const double value =
            L.cost + c_km_ * m.dist(node_[L.node], depot_) + work_cost(static_cast<double>(theta)) + c_fix_ - idle_;
        if (value < res.min_value) res.min_value = value;
        if (value < -1e-9) done.push_back({value, idx});
      }
    }
    for (std::size_t j = 0; j < n_; ++j) extend(idx, j);
  }
  res.labels = labels.size();
  std::ranges::sort(done);
  for (std::size_t t = 0; t < done.size() && static_cast<int>(res.best.size()) < o.keep; ++t) {
    RelaxedRoute rr;
    rr.value = done[t].first;
    for (int x = done[t].second; x >= 0; x = labels[static_cast<std::size_t>(x)].parent) {
      rr.seq.push_back(labels[static_cast<std::size_t>(x)].node);  // break labels point to the previous stop
    }
    std::ranges::reverse(rr.seq);
    res.best.push_back(std::move(rr));
  }
  return res;
}

}  // namespace legalvrp::cg
