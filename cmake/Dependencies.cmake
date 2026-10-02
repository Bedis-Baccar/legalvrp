# Third-party dependencies, pinned to exact releases (the C++ equivalent of a lock file).
# Fetched once at configure time into the build tree; nothing is downloaded while
# solving or testing (PROJECT_BRIEF §0.7).
include(FetchContent)
set(FETCHCONTENT_QUIET ON)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

# JSON I/O of instances, plans, stats (header-only).
FetchContent_Declare(nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
  DOWNLOAD_EXTRACT_TIMESTAMP ON)

# YAML configs (config/*.yaml).
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
set(YAML_CPP_INSTALL OFF CACHE BOOL "" FORCE)
set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(yaml-cpp
  GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
  GIT_TAG        0.8.0
  GIT_SHALLOW    ON)

# Command-line parsing for the apps (header-only).
FetchContent_Declare(CLI11
  GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
  GIT_TAG        v2.5.0
  GIT_SHALLOW    ON)

FetchContent_MakeAvailable(nlohmann_json yaml-cpp CLI11)

# yaml-cpp 0.8.0 declares cmake_minimum_required < 3.5, rejected by CMake 4.x.
# Handled by CMAKE_POLICY_VERSION_MINIMUM in CMakePresets.json.

if(LEGALVRP_BUILD_TESTS)
  set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
  set(CATCH_INSTALL_EXTRAS OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.8.1
    GIT_SHALLOW    ON)
  FetchContent_MakeAvailable(Catch2)
  list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()
