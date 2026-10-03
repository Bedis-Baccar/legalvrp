include(Catch)

# Test labels:
#   unit    — pure C++, no solver; runs everywhere (CI).
#   gurobi  — needs a Gurobi licence; MILP tests use `tiny` instances only.
function(legalvrp_test name label)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE Catch2::Catch2WithMain legalvrp::domain)
  legalvrp_set_warnings(${name})
  catch_discover_tests(${name}
    PROPERTIES LABELS ${label}
    DISCOVERY_MODE PRE_TEST)   # discovery at ctest time: no DLL lookup at build time
endfunction()

legalvrp_test(test_smoke  unit unit/test_smoke.cpp)
legalvrp_test(test_config unit unit/test_config.cpp)
target_link_libraries(test_config PRIVATE yaml-cpp::yaml-cpp)
legalvrp_test(test_layering unit unit/test_layering.cpp)
legalvrp_test(test_rules  unit unit/test_rules.cpp)
legalvrp_test(test_compat unit unit/test_compat.cpp)
legalvrp_test(test_generate unit unit/test_generate.cpp)
target_link_libraries(test_generate PRIVATE legalvrp::data legalvrp::estimate legalvrp::week)

if(LEGALVRP_WITH_GUROBI)
  legalvrp_test(test_gurobi_smoke gurobi unit/test_gurobi_smoke.cpp)
  target_link_libraries(test_gurobi_smoke PRIVATE legalvrp::model)
  legalvrp_test(test_milp gurobi unit/test_milp.cpp)
  target_link_libraries(test_milp PRIVATE legalvrp::week legalvrp::model)
  legalvrp_test(test_solve gurobi unit/test_solve.cpp)
  target_link_libraries(test_solve PRIVATE legalvrp::week legalvrp::model legalvrp::data)
  legalvrp_test(test_week_loop gurobi unit/test_week_loop.cpp)
  target_link_libraries(test_week_loop PRIVATE legalvrp::week legalvrp::kpi)
endif()

legalvrp_test(test_route_eval unit unit/test_route_eval.cpp)
target_link_libraries(test_route_eval PRIVATE legalvrp::heuristics legalvrp::data)
legalvrp_test(test_day unit unit/test_day.cpp)
legalvrp_test(test_checker_corpus unit unit/test_checker_corpus.cpp)
target_link_libraries(test_checker_corpus PRIVATE legalvrp::check)
legalvrp_test(test_checker unit unit/test_checker.cpp)
target_link_libraries(test_checker PRIVATE legalvrp::check legalvrp::heuristics legalvrp::data)
legalvrp_test(test_territory unit unit/test_territory.cpp)
target_link_libraries(test_territory PRIVATE legalvrp::week)
legalvrp_test(test_kpi unit unit/test_kpi.cpp)
target_link_libraries(test_kpi PRIVATE legalvrp::kpi legalvrp::week)
