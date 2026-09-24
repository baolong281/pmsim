cmake_minimum_required(VERSION 3.24)
project(pmsim VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(PMSIM_BUILD_TESTS     "Build C++ tests"             ON)
option(PMSIM_BUILD_BENCH     "Build C++ benchmarks"        OFF)
option(PMSIM_BUILD_PYTHON    "Build the Python extension"  OFF)
option(PMSIM_BUILD_COLLECTOR "Build the collector binary"  OFF)
option(PMSIM_SANITIZE        "Build with ASan and UBSan"   OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

add_library(pmsim_warnings INTERFACE)
target_compile_options(pmsim_warnings INTERFACE
  $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Wconversion -Wshadow>)

if(PMSIM_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

include(FetchContent)

add_subdirectory(cpp)

if(PMSIM_BUILD_COLLECTOR)
  add_subdirectory(collector)
endif()
