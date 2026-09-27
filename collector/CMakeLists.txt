find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Boost 1.81 CONFIG REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZSTD REQUIRED IMPORTED_TARGET libzstd)

FetchContent_Declare(simdjson
  URL https://github.com/simdjson/simdjson/archive/refs/tags/v3.10.1.tar.gz)
FetchContent_MakeAvailable(simdjson)

FetchContent_Declare(spdlog
  URL https://github.com/gabime/spdlog/archive/refs/tags/v1.17.0.tar.gz)
FetchContent_MakeAvailable(spdlog)

add_executable(pmsim-collector src/main.cpp)
target_include_directories(pmsim-collector PRIVATE include)
target_link_libraries(pmsim-collector PRIVATE
  pmsim_warnings
  Threads::Threads
  OpenSSL::SSL
  OpenSSL::Crypto
  Boost::headers
  PkgConfig::ZSTD
  simdjson::simdjson
  spdlog::spdlog
)
