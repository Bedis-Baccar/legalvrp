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

legalvrp_app(legalvrp-generate generate.cpp legalvrp::week)  # data + baseline certification
legalvrp_app(legalvrp-check    check.cpp    legalvrp::check legalvrp::data)  # data: instance files only
legalvrp_app(legalvrp-run-week run_week.cpp legalvrp::week legalvrp::kpi legalvrp::alns)

if(LEGALVRP_WITH_GUROBI)
  legalvrp_app(legalvrp-scaling scaling.cpp legalvrp::week legalvrp::model legalvrp::data)
  legalvrp_app(legalvrp-bench   bench.cpp    legalvrp::week legalvrp::model)  # MILP vs baseline (T6/T7)
  legalvrp_app(legalvrp-myopia  myopia.cpp   legalvrp::week legalvrp::model legalvrp::kpi)  # T11
endif()
