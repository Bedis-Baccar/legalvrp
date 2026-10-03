#pragma once
// legalvrp::model — solution transfer (implemented in extract.cpp as MilpModel members):
//   MilpModel::set_start  complete MIP start from a legal plan (§6.6, S6)
//   MilpModel::extract    incumbent -> DayPlan. Departure and service starts are the model's
//                         values rounded to integer minutes; arrivals and the return are
//                         recomputed from the data, so the plan is what the driver executes.
// The plan must still pass the checker (week/solve_day does this).

#include "legalvrp/model/milp_day.hpp"
