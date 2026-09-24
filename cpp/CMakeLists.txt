add_library(pmsim_core STATIC
  src/book.cpp
  src/replay.cpp
  src/sim.cpp
  src/fill_models.cpp
  src/fees.cpp
  src/portfolio.cpp
  src/ledger.cpp
)
target_include_directories(pmsim_core PUBLIC include)
target_link_libraries(pmsim_core PRIVATE pmsim_warnings)
set_target_properties(pmsim_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(PMSIM_BUILD_TESTS)
  FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  enable_testing()
  add_executable(pmsim_tests
    tests/test_book.cpp
    tests/test_replay.cpp
    tests/test_sim.cpp
    tests/test_fill_models.cpp
    tests/test_fees.cpp
    tests/test_portfolio.cpp
  )
  target_link_libraries(pmsim_tests PRIVATE pmsim_core pmsim_warnings GTest::gtest_main)
  include(GoogleTest)
  gtest_discover_tests(pmsim_tests)
endif()

if(PMSIM_BUILD_BENCH)
  FetchContent_Declare(benchmark
    URL https://github.com/google/benchmark/archive/refs/tags/v1.9.1.tar.gz)
  set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(benchmark)

  add_executable(pmsim_bench
    bench/bench_book.cpp
    bench/bench_replay.cpp
  )
  target_link_libraries(pmsim_bench PRIVATE pmsim_core benchmark::benchmark_main)
endif()

if(PMSIM_BUILD_PYTHON)
  find_package(Python 3.11 REQUIRED COMPONENTS Interpreter Development.Module)
  execute_process(
    COMMAND "${Python_EXECUTABLE}" -m nanobind --cmake_dir
    OUTPUT_STRIP_TRAILING_WHITESPACE OUTPUT_VARIABLE nanobind_ROOT)
  find_package(nanobind CONFIG REQUIRED)

  nanobind_add_module(_core bindings/module.cpp)
  target_link_libraries(_core PRIVATE pmsim_core)
  install(TARGETS _core LIBRARY DESTINATION pmsim)
endif()
