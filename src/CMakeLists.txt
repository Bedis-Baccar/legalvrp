# One static library per module. The link graph below IS the architecture:
# a module can only include headers of the modules it links.
#
#   domain  <- data, estimate, heuristics, check, model, week, kpi
#   check   <- domain only           (PROJECT_BRIEF §0.4: never model/heuristics/week)
#   model   <- domain, Gurobi        (MIP start arrives as a DayPlan, not via heuristics)
#   week    <- domain, heuristics, check, [model]
#   kpi     <- domain, check         (KPIs from the checker's recomputation, §11)
#
# tests/unit/test_layering.cpp additionally scans #include lines.

function(legalvrp_module name)
  cmake_parse_arguments(M "" "" "SOURCES;DEPS" ${ARGN})
  add_library(legalvrp_${name} STATIC ${M_SOURCES})
  add_library(legalvrp::${name} ALIAS legalvrp_${name})
  target_include_directories(legalvrp_${name} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
  target_compile_features(legalvrp_${name} PUBLIC cxx_std_20)
  target_link_libraries(legalvrp_${name} PUBLIC ${M_DEPS})
  legalvrp_set_warnings(legalvrp_${name})
endfunction()

set(L legalvrp)

legalvrp_module(domain
  SOURCES ${L}/domain/models.cpp ${L}/domain/rules.cpp ${L}/domain/compat.cpp ${L}/domain/paths.cpp
          ${L}/domain/json.cpp ${L}/domain/day.cpp ${L}/domain/detail/yaml_section.cpp
  DEPS nlohmann_json::nlohmann_json)
target_link_libraries(legalvrp_domain PRIVATE yaml-cpp::yaml-cpp)  # yaml stays out of public headers
target_compile_definitions(legalvrp_domain PUBLIC
  LEGALVRP_SOURCE_ROOT="${LEGALVRP_SOURCE_ROOT}"
  LEGALVRP_VERSION="${PROJECT_VERSION}")

legalvrp_module(data
  SOURCES ${L}/data/rng.cpp ${L}/data/instance_config.cpp ${L}/data/synthetic.cpp
          ${L}/data/orders.cpp ${L}/data/roster.cpp
          ${L}/data/matrix.cpp ${L}/data/io.cpp ${L}/data/generate.cpp
  DEPS legalvrp::domain)
target_link_libraries(legalvrp_data PRIVATE yaml-cpp::yaml-cpp)  # instance_config uses the internal YAML reader

legalvrp_module(estimate
  SOURCES ${L}/estimate/deterministic.cpp
  DEPS legalvrp::domain)

legalvrp_module(heuristics
  SOURCES ${L}/heuristics/route_eval.cpp ${L}/heuristics/territory.cpp
          ${L}/heuristics/kmeans.cpp ${L}/heuristics/assignment.cpp ${L}/heuristics/enumerate.cpp
  DEPS legalvrp::domain)

legalvrp_module(check
  SOURCES ${L}/check/checker.cpp
  DEPS legalvrp::domain)

legalvrp_module(kpi
  SOURCES ${L}/kpi/kpis.cpp ${L}/kpi/report.cpp
  DEPS legalvrp::domain legalvrp::check)

if(LEGALVRP_WITH_GUROBI)
  legalvrp_module(model
    SOURCES ${L}/model/bigm.cpp ${L}/model/milp_day.cpp ${L}/model/solve.cpp ${L}/model/extract.cpp
            ${L}/model/clairvoyant.cpp
            ${L}/model/cuts.cpp
    DEPS legalvrp::domain Gurobi::Gurobi)
  target_compile_definitions(legalvrp_model PUBLIC LEGALVRP_HAS_GUROBI=1)
  target_link_libraries(legalvrp_model PRIVATE yaml-cpp::yaml-cpp)  # solver.yaml
  set(_week_model legalvrp::model)
endif()

legalvrp_module(week
  SOURCES ${L}/week/state.cpp ${L}/week/loop.cpp ${L}/week/certify.cpp
  DEPS legalvrp::domain legalvrp::data legalvrp::heuristics legalvrp::check ${_week_model})
if(LEGALVRP_WITH_GUROBI)
  target_sources(legalvrp_week PRIVATE ${L}/week/solve_day.cpp)  # MILP + checker orchestration
endif()
