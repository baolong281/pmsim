BUILD_TYPE ?= RelWithDebInfo
JOBS       ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu)
CMAKE_ARGS ?=

BUILD_DIR     := build/cpp
BENCH_DIR     := build/bench
COLLECTOR_DIR := build/collector
ASAN_DIR      := build/asan
DEV_DIR       := build/dev

CPP_SOURCES = $(shell find cpp collector -name '*.hpp' -o -name '*.cpp')

.PHONY: all help build test test-cpp test-py py bench collector sanitize compdb fmt lint clean

all: build

help:
	@echo "build      configure and build the C++ core and tests"
	@echo "test       run C++ and Python tests"
	@echo "test-cpp   run C++ tests only"
	@echo "test-py    build the Python package and run pytest"
	@echo "py         (re)install the Python package with the extension"
	@echo "bench      build and run C++ benchmarks (Release)"
	@echo "collector  build the collector binary (Release)"
	@echo "sanitize   build and run C++ tests with ASan and UBSan"
	@echo "compdb     generate compile_commands.json for clangd"
	@echo "fmt        format C++ and Python"
	@echo "lint       lint Python"
	@echo "clean      remove build output"

build:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DPMSIM_BUILD_TESTS=ON $(CMAKE_ARGS)
	cmake --build $(BUILD_DIR) -j $(JOBS)

test: test-cpp test-py

test-cpp: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure

py:
	uv sync --reinstall-package pmsim

test-py: py
	uv run pytest || [ $$? -eq 5 ]

bench:
	cmake -S . -B $(BENCH_DIR) -DCMAKE_BUILD_TYPE=Release -DPMSIM_BUILD_TESTS=OFF -DPMSIM_BUILD_BENCH=ON $(CMAKE_ARGS)
	cmake --build $(BENCH_DIR) -j $(JOBS)
	./$(BENCH_DIR)/cpp/pmsim_bench

collector:
	cmake -S . -B $(COLLECTOR_DIR) -DCMAKE_BUILD_TYPE=Release -DPMSIM_BUILD_TESTS=OFF -DPMSIM_BUILD_COLLECTOR=ON $(CMAKE_ARGS)
	cmake --build $(COLLECTOR_DIR) -j $(JOBS) --target pmsim-collector

sanitize:
	cmake -S . -B $(ASAN_DIR) -DCMAKE_BUILD_TYPE=Debug -DPMSIM_BUILD_TESTS=ON -DPMSIM_SANITIZE=ON $(CMAKE_ARGS)
	cmake --build $(ASAN_DIR) -j $(JOBS)
	ctest --test-dir $(ASAN_DIR) --output-on-failure

compdb:
	cmake -S . -B $(DEV_DIR) -DCMAKE_BUILD_TYPE=Debug -DPMSIM_BUILD_TESTS=ON -DPMSIM_BUILD_BENCH=ON -DPMSIM_BUILD_COLLECTOR=ON $(CMAKE_ARGS)
	ln -sf $(DEV_DIR)/compile_commands.json compile_commands.json

fmt:
	clang-format -i $(CPP_SOURCES)
	uv run ruff format python tests

lint:
	uv run ruff check python tests

clean:
	rm -rf build
