#pragma once
#include "spdlog/fmt/bundled/format.h"
#include "spdlog/fmt/bundled/ranges.h"
#include <deque>

using id = uint32_t;

class SessionStats {
public:
  explicit SessionStats(id session) : session{session} {}
  void inc_bytes_recv(long long b) { bytes_recv += b; }
  void inc_messages() { messages_recv++; }
  // returns the new total
  long long inc_dropped() { return ++messages_dropped; }

  friend std::string format_as(SessionStats const &s) {
    return fmt::format("session={}, messages={} bytes={} dropped={}",
                       s.session, s.messages_recv, s.bytes_recv,
                       s.messages_dropped);
  }

private:
  id session;
  long long bytes_recv{0};
  long long messages_recv{0};
  long long messages_dropped{0};
};

class NetworkStats {
public:
  SessionStats &add_session(id session) {
    return sessions.emplace_back(session);
  }

  friend std::string format_as(NetworkStats const &n) {
    return fmt::format("{}", fmt::join(n.sessions, " | "));
  }

private:
  std::deque<SessionStats> sessions;
};
