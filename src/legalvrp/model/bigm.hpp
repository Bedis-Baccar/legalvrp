#pragma once
// legalvrp::model — preprocessing for the daily MILP: compatibility sets, time-window
// reduction, arc pruning, shortest paths, driving bounds, forced breaks (docs/MODEL.md §6.1,
// §6.5; docs/FORMULATION_RESEARCH.md S2-S5).
//
// Node numbering inside the model: orders 0..n-1, depot_out = n (departure), depot_in = n + 1
// (depot return). Every reduction here is valid: it removes only arcs and window points
// that no legal route can use.

#include <cstddef>
#include <vector>

#include "legalvrp/domain/models.hpp"

namespace legalvrp::model {

enum class Formulation {
  strong,     // D-021: node-indexed T/D, reduced windows, extended pruning, tight M, cuts
  reference,  // brief §6 as amended (D-018): three-index T/D, brief pruning and big-M
};

[[nodiscard]] const char* to_string(Formulation f) noexcept;

struct Arc {
  int from = 0;      // order index or depot_out
  int to = 0;        // order index or depot_in
  Minutes tau = 0;   // travel minutes
  double km = 0.0;
};

struct Prep {
  Formulation formulation = Formulation::strong;
  int n = 0;                          // orders
  int K = 0;                          // drivers
  int depot_out = 0, depot_in = 0;  // model nodes n and n + 1
  std::size_t depot_node = 0;         // matrix index
  std::vector<std::size_t> node;      // matrix index per order
  std::vector<Minutes> e, l, s;       // service-start window (reduced in strong), service
  std::vector<Pallets> q;
  std::vector<Euros> penalty;
  std::vector<bool> servable;         // false: no driver can serve it legally -> u_i = 1
  std::vector<std::vector<bool>> compat;  // [k][i]: i in C_k
  std::vector<std::vector<Arc>> arcs;     // per driver, includes (depot_out, depot_in)
  std::vector<Minutes> D_lb, D_ub;    // driving on arrival at i
  std::vector<std::vector<Minutes>> sp;   // shortest travel among orders 0..n-1 and depot n
  std::vector<std::vector<bool>> force_break;  // [k][i]: serving i forces a break (strong)

  std::vector<Minutes> tt;            // travel minutes over model nodes 0..n+1, (n+2)^2
  std::vector<double> dd;             // km over model nodes
  [[nodiscard]] Minutes tau(int from, int to) const {
    return tt[static_cast<std::size_t>(from) * static_cast<std::size_t>(n + 2) + static_cast<std::size_t>(to)];
  }
  [[nodiscard]] double km(int from, int to) const {
    return dd[static_cast<std::size_t>(from) * static_cast<std::size_t>(n + 2) + static_cast<std::size_t>(to)];
  }

  // statistics
  int arcs_total = 0;
  int arcs_full = 0;                  // before pruning: K * (n(n-1) + 2n + 1)
  int window_minutes_removed = 0;     // sum over orders of (l - e) reduction
};

[[nodiscard]] Prep prepare(const DayInstance& day, Formulation f);

// max(0, x): the smallest big-M that switches a constraint off, from the bound analysis.
[[nodiscard]] constexpr double big_m(double x) noexcept { return x > 0.0 ? x : 0.0; }

}  // namespace legalvrp::model
