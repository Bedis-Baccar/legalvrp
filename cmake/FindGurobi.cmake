# FindGurobi.cmake — locates the Gurobi C and C++ libraries.
#
# Input : GUROBI_HOME (CMake or environment variable), e.g. C:/gurobi1203/win64
# Output: imported target Gurobi::Gurobi (C++ API + C library), Gurobi_VERSION_MAJOR
#
# Windows: the C++ wrapper is shipped prebuilt for MSVC only (gurobi_c++md2017.lib
# for /MD, gurobi_c++mdd2017.lib for /MDd). MinGW cannot link it; use MSVC.

set(_grb_hints "${GUROBI_HOME}" "$ENV{GUROBI_HOME}")
if(WIN32)
  file(GLOB _grb_dirs "C:/gurobi*/win64")
  list(SORT _grb_dirs ORDER DESCENDING)
  list(APPEND _grb_hints ${_grb_dirs})
endif()

find_path(GUROBI_INCLUDE_DIR NAMES gurobi_c++.h HINTS ${_grb_hints} PATH_SUFFIXES include)

# Version from gurobi_c.h
if(GUROBI_INCLUDE_DIR AND EXISTS "${GUROBI_INCLUDE_DIR}/gurobi_c.h")
  file(STRINGS "${GUROBI_INCLUDE_DIR}/gurobi_c.h" _v REGEX "#define GRB_VERSION_(MAJOR|MINOR|TECHNICAL)")
  string(REGEX REPLACE ".*GRB_VERSION_MAJOR[ \t]+([0-9]+).*" "\\1" Gurobi_VERSION_MAJOR "${_v}")
  string(REGEX REPLACE ".*GRB_VERSION_MINOR[ \t]+([0-9]+).*" "\\1" Gurobi_VERSION_MINOR "${_v}")
  string(REGEX REPLACE ".*GRB_VERSION_TECHNICAL[ \t]+([0-9]+).*" "\\1" Gurobi_VERSION_PATCH "${_v}")
  set(Gurobi_VERSION "${Gurobi_VERSION_MAJOR}.${Gurobi_VERSION_MINOR}.${Gurobi_VERSION_PATCH}")
endif()

find_library(GUROBI_C_LIBRARY
  NAMES "gurobi${Gurobi_VERSION_MAJOR}${Gurobi_VERSION_MINOR}" "gurobi${Gurobi_VERSION_MAJOR}0"
  HINTS ${_grb_hints} PATH_SUFFIXES lib)

if(MSVC)
  find_library(GUROBI_CXX_LIBRARY       NAMES gurobi_c++md2017  HINTS ${_grb_hints} PATH_SUFFIXES lib)
  find_library(GUROBI_CXX_LIBRARY_DEBUG NAMES gurobi_c++mdd2017 HINTS ${_grb_hints} PATH_SUFFIXES lib)
else()
  find_library(GUROBI_CXX_LIBRARY NAMES gurobi_c++ HINTS ${_grb_hints} PATH_SUFFIXES lib)
  set(GUROBI_CXX_LIBRARY_DEBUG "${GUROBI_CXX_LIBRARY}")
endif()

# Runtime DLL (Windows) so apps/tests can be copied next to it or have PATH set.
if(WIN32 AND GUROBI_INCLUDE_DIR)
  get_filename_component(_grb_root "${GUROBI_INCLUDE_DIR}" DIRECTORY)
  file(GLOB GUROBI_RUNTIME_DLL "${_grb_root}/bin/gurobi${Gurobi_VERSION_MAJOR}*.dll")
  list(FILTER GUROBI_RUNTIME_DLL EXCLUDE REGEX "light|NET|Jni")
  set(GUROBI_BIN_DIR "${_grb_root}/bin" CACHE PATH "Gurobi runtime directory")
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Gurobi
  REQUIRED_VARS GUROBI_INCLUDE_DIR GUROBI_C_LIBRARY GUROBI_CXX_LIBRARY
  VERSION_VAR Gurobi_VERSION)

if(Gurobi_FOUND AND NOT TARGET Gurobi::Gurobi)
  add_library(Gurobi::C UNKNOWN IMPORTED)
  set_target_properties(Gurobi::C PROPERTIES
    IMPORTED_LOCATION "${GUROBI_C_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${GUROBI_INCLUDE_DIR}")

  add_library(Gurobi::Gurobi INTERFACE IMPORTED)
  target_link_libraries(Gurobi::Gurobi INTERFACE
    "$<IF:$<CONFIG:Debug>,${GUROBI_CXX_LIBRARY_DEBUG},${GUROBI_CXX_LIBRARY}>"
    Gurobi::C)
endif()

mark_as_advanced(GUROBI_INCLUDE_DIR GUROBI_C_LIBRARY GUROBI_CXX_LIBRARY GUROBI_CXX_LIBRARY_DEBUG)
