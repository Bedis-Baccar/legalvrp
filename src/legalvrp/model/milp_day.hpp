#pragma once
// legalvrp::model — the daily MILP (docs/MODEL.md, constraints C1-C18 with the amendments
// D-018 fixed start, D-019 penalties, D-020 daytime duties).
//
// Two formulations (docs/FORMULATION_RESEARCH.md, D-021):
//   reference  brief §6: T[i,k], D[i,k] per driver; brief pruning and big-M values.
//   strong     T[i], D[i] per order with the aggregated arc flow X_ij = sum_k x[i,j,k] in C6/C9;
//              reduced windows and extended pruning (bigm.hpp); big-M from bounds; valid
//              inequalities: X_ij + X_ji <= 1, visit <= used, capacity * used,
//              drive <= DB (1 + brk), visit <= brk when serving i forces a break.
// Both keep C8-C9 as equalities on used arcs (two-sided), the break delay in C6-C7, and the
// symmetry rule for identical drivers (§6.6). Variables and constraints carry the brief's code
// names and the ids of orders and drivers.

#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "gurobi_c++.h"
#include "legalvrp/domain/models.hpp"
#include "legalvrp/model/bigm.hpp"
#include "legalvrp/model/cuts.hpp"

namespace legalvrp::model {

class MilpModel {
 public:
  MilpModel(GRBEnv& env, const DayInstance& day, Formulation f);

  [[nodiscard]] GRBModel& grb() noexcept { return model_; }
  [[nodiscard]] const Prep& prep() const noexcept { return prep_; }
  [[nodiscard]] const DayInstance& day() const noexcept { return day_; }
  [[nodiscard]] int binaries() const noexcept { return binaries_; }

  // Complete MIP start from a legal plan (§6.6, S6). Returns false if the plan uses an arc
  // the preprocessing removed or serves an order it marked unservable (then no start is set).
  bool set_start(const DayPlan& plan);

  // Connectivity user cuts (D-030) over this model's arc variables; the caller owns the
  // callback and must keep it alive during optimize().
  [[nodiscard]] std::unique_ptr<ConnectivityCuts> make_connectivity_cuts(bool per_driver) const;

  // The incumbent as a plan: sequences, break nodes, departure and service starts from the
  // model (rounded to integer minutes); arrivals and return recomputed from the data.
  [[nodiscard]] DayPlan extract() const;

 private:
  [[nodiscard]] GRBVar& T(int i, int k);
  [[nodiscard]] GRBVar& D(int i, int k);
  [[nodiscard]] const GRBVar& T(int i, int k) const;
  void build();

  const DayInstance& day_;
  Prep prep_;
  GRBModel model_;
  int binaries_ = 0;

  std::vector<std::map<std::pair<int, int>, GRBVar>> x_;  // per driver, arc -> var
  std::vector<GRBVar> u_;
  std::vector<GRBVar> Tn_, Dn_;                            // strong: per order
  std::vector<std::vector<GRBVar>> Tk_, Dk_;               // reference: [k][i]
  std::vector<std::vector<GRBVar>> y_;                     // [k][i], valid iff compat
  std::vector<GRBVar> t0_, tE_, a_, b_, svc_, ext_;
};

}  // namespace legalvrp::model
