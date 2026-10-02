cmake_minimum_required(VERSION 3.25)

project(legalvrp
  VERSION 0.1.0
  DESCRIPTION "Legal VRP with driver hours-of-service rules - V0 (exact daily MILP)"
  LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(LEGALVRP_WITH_GUROBI   "Build the MILP module (requires Gurobi C++ API)" ON)
option(LEGALVRP_BUILD_TESTS   "Build unit tests"                                 ON)
option(LEGALVRP_WARNINGS_AS_ERRORS "Treat compiler warnings as errors"          OFF)
option(LEGALVRP_ENABLE_LTO    "Interprocedural optimisation in Release"          OFF)

if(PROJECT_IS_TOP_LEVEL)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
  if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
  endif()
endif()

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Dependencies)

if(LEGALVRP_ENABLE_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT _ipo_ok OUTPUT _ipo_msg)
  if(_ipo_ok)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
  endif()
endif()

if(LEGALVRP_WITH_GUROBI)
  find_package(Gurobi REQUIRED)
endif()

# Absolute path of the repository, so executables and tests find config/ and
# tests/fixtures/ regardless of the working directory.
set(LEGALVRP_SOURCE_ROOT "${PROJECT_SOURCE_DIR}")

add_subdirectory(src)
add_subdirectory(apps)

if(LEGALVRP_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

message(STATUS "legalvrp ${PROJECT_VERSION}: Gurobi=${LEGALVRP_WITH_GUROBI} tests=${LEGALVRP_BUILD_TESTS}")
