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
target_link_libraries(test_generate PRIVATE legalvrp::data legalvrp::estimate)

if(LEGALVRP_WITH_GUROBI)
  legalvrp_test(test_gurobi_smoke gurobi unit/test_gurobi_smoke.cpp)
  target_link_libraries(test_gurobi_smoke PRIVATE legalvrp::model)
endif()

legalvrp_test(test_route_eval unit unit/test_route_eval.cpp)
target_link_libraries(test_route_eval PRIVATE legalvrp::heuristics legalvrp::data)
legalvrp_test(test_day unit unit/test_day.cpp)
