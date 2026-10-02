# Executables = the brief's module CLIs (§10 "Entry points").
#   legalvrp-generate  --config ... --seed ...     (python -m legalvrp.data.generate)
#   legalvrp-run-week  --instance ...              (python -m legalvrp.experiments.run_week)
#   legalvrp-scaling   --config ...                (python -m legalvrp.experiments.scaling)
#   legalvrp-check     --instance ... --plan ...   (python -m legalvrp.check.checker)

function(legalvrp_app name src)
  add_executable(${name} ${src})
  target_link_libraries(${name} PRIVATE CLI11::CLI11 ${ARGN})
  legalvrp_set_warnings(${name})
endfunction()

legalvrp_app(legalvrp-generate generate.cpp legalvrp::data legalvrp::heuristics legalvrp::check)
legalvrp_app(legalvrp-check    check.cpp    legalvrp::check)   # checker app links nothing else
legalvrp_app(legalvrp-run-week run_week.cpp legalvrp::week legalvrp::kpi)

if(LEGALVRP_WITH_GUROBI)
  legalvrp_app(legalvrp-scaling scaling.cpp legalvrp::model legalvrp::heuristics legalvrp::data)
endif()
