#pragma once
// legalvrp::model — connectivity cuts (D-030), separated as Gurobi user cuts.
//
// For a set S of customers and a customer i in S, every route serving i starts at the depot,
// which is outside S, so the aggregated flow entering S is at least i's visit:
//     sum_{a not in S, b in S} sum_k x[a,b,k]  >=  sum_k visit[i,k].
// Integer solutions already satisfy it (the time constraints C6-C7 exclude subtours), so these
// are cuts on fractional nodes only: they forbid the LP from serving remote orders through
// small fractional cycles. The per-driver version (each driver's own flow) is stronger. Separation: for each customer with visit > 0.1, a max-flow
// depot -> i on the support graph with capacities X_ab; if the max-flow is below visit by more
// than 0.05, the sink side of the min cut is S (Bard, Kontoravdis & Yu 2002; Kallehauge 2008).

#include <cstddef>
#include <set>
#include <string>
#include <vector>

#include "gurobi_c++.h"

namespace legalvrp::model {

struct CutArc {
  int from;      // customer index or n (depot)
  int to;        // customer index
  int k;         // driver index
  GRBVar var;    // x[from,to,k]
};

class ConnectivityCuts final : public GRBCallback {
 public:
  // per_driver: also separate the per-driver cuts sum_{a not in S, b in S} x[a,b,k] >= visit[i,k]
  // (stronger: one driver cannot use another's inflow) on the first `per_driver_nodes` nodes.
  ConnectivityCuts(int n, int K, std::vector<CutArc> arcs, bool per_driver, double per_driver_nodes = 1000);
  [[nodiscard]] long long cuts_added() const noexcept { return cuts_; }

 protected:
  void callback() override;

 private:
  int n_;
  int K_;
  bool per_driver_;
  double per_driver_nodes_;
  std::vector<CutArc> arcs_;
  std::vector<GRBVar> vars_;
  long long cuts_ = 0;
  std::set<std::string> seen_;  // sets already cut (as bitstrings)
};

}  // namespace legalvrp::model
