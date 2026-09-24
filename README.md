# pmsim

Backtesting and research for prediction markets.

## Requirements

- C++20 compiler (GCC 12+ or Clang 15+), CMake 3.24+
- Python 3.11+ and uv
- Collector only: Boost 1.81+, OpenSSL, zstd, pkg-config

## Commands

Run `make help` for the full list.

| Command | Does |
| --- | --- |
| `make build` | Build the C++ core and tests |
| `make test` | Run C++ and Python tests |
| `make py` | Install the Python package with the C++ extension |
| `make bench` | Build and run benchmarks |
| `make collector` | Build the collector |
| `make sanitize` | Run C++ tests under ASan and UBSan |
