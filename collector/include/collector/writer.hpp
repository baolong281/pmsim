#pragma once

#include "spdlog/spdlog.h"
#include <atomic>
#include <chrono>
#include <collector/Session.hpp>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <thread>

// appends records as JSON lines to <out_dir>/<venue>/<YYYY-MM-DD>/<HH>.jsonl,
// one file per UTC hour of recv time, so closed hours can be compressed
template <typename Q> class Writer {
public:
  Writer(std::filesystem::path out_dir, std::string const &venue)
      : root{std::move(out_dir) / venue} {}
  ~Writer() {
    if (file)
      fclose(file);
  }
  Writer(Writer const &) = delete;
  Writer &operator=(Writer const &) = delete;
  Writer(Writer &&) = delete;
  Writer &operator=(Writer &&) = delete;

  void run(Q &queue) {
    running = true;
    auto write_one = [this](RawRecord const &r) { write(r); };
    while (running) {
      auto n = queue.consume_all(write_one);

      // sleep when idle instead of spinning a core at 100%
      if (n == 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // final write if anything else gets added
    queue.consume_all(write_one);
    if (file)
      fflush(file);
  }

  void stop() { running = false; }

private:
  static constexpr int64_t ns_per_hour = 3'600'000'000'000;

  bool running{false};
  std::filesystem::path root;
  FILE *file{nullptr};
  int64_t hour{-1}; // hours since epoch of the open file

  void write(RawRecord const &r) {
    if (r.recv_ns / ns_per_hour != hour)
      rotate(r.recv_ns / ns_per_hour);

    std::string line;
    line.reserve(r.data.size() + 128);
    line += R"({"recv_ns":)" + std::to_string(r.recv_ns);
    line += R"(,"session":)" + std::to_string(r.session);
    line += R"(,"attempt":)" + std::to_string(r.attempt);
    line += R"(,"seq":)" + std::to_string(r.seq);
    line += R"(,"payload":)" + r.data + "}\n";
    std::fwrite(line.data(), 1, line.size(), file);
  }

  void rotate(int64_t new_hour) {
    if (file)
      fclose(file);

    std::time_t t = static_cast<std::time_t>(new_hour * 3600);
    std::tm utc{};
    gmtime_r(&t, &utc);
    char date[11], hh[3];
    std::strftime(date, sizeof date, "%Y-%m-%d", &utc);
    std::strftime(hh, sizeof hh, "%H", &utc);

    auto dir = root / date;
    std::filesystem::create_directories(dir);
    auto path = dir / (std::string(hh) + ".jsonl");

    spdlog::info("opening file: {}", path.string());

    file = fopen(path.c_str(), "ab");
    if (!file)
      throw std::runtime_error("Error opening file: " + path.string());
    hour = new_hour;
  }
};
