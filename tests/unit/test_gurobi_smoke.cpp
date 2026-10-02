// Toolchain check: Gurobi C++ API links, the licence is found, a tiny MIP solves.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "gurobi_c++.h"

TEST_CASE("Gurobi solves a tiny knapsack to optimality", "[gurobi]") {
  GRBEnv env(true);
  env.set(GRB_IntParam_OutputFlag, 0);
  env.start();
  GRBModel m(env);
  // max 5a + 4b + 3c  s.t.  2a + 3b + c <= 4,  binaries  -> a = c = 1, objective 8
  GRBVar a = m.addVar(0, 1, 5, GRB_BINARY, "a");
  GRBVar b = m.addVar(0, 1, 4, GRB_BINARY, "b");
  GRBVar c = m.addVar(0, 1, 3, GRB_BINARY, "c");
  m.set(GRB_IntAttr_ModelSense, GRB_MAXIMIZE);
  m.addConstr(2 * a + 3 * b + c <= 4, "cap");
  m.optimize();
  REQUIRE(m.get(GRB_IntAttr_Status) == GRB_OPTIMAL);
  CHECK(m.get(GRB_DoubleAttr_ObjVal) == Catch::Approx(8.0));
  CHECK(a.get(GRB_DoubleAttr_X) > 0.5);
  CHECK(c.get(GRB_DoubleAttr_X) > 0.5);
}

// `small` and `scale` exceed 2000 variables/constraints (docs/SIZING.md §2.3).
// A size-limited licence throws here; the academic licence must not.
TEST_CASE("Gurobi licence is not size-restricted", "[gurobi]") {
  GRBEnv env(true);
  env.set(GRB_IntParam_OutputFlag, 0);
  env.start();
  GRBModel m(env);
  constexpr int n = 2500;
  GRBLinExpr sum = 0;
  for (int i = 0; i < n; ++i) {
    GRBVar v = m.addVar(0, 1, 1.0, GRB_BINARY);
    sum += v;
    m.addConstr(v <= 1);
  }
  m.addConstr(sum >= 3);
  REQUIRE_NOTHROW(m.optimize());
  REQUIRE(m.get(GRB_IntAttr_Status) == GRB_OPTIMAL);
  CHECK(m.get(GRB_DoubleAttr_ObjVal) == Catch::Approx(3.0));
}
