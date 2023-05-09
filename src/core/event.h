#pragma once

#include <unordered_map>

#include "channel.h"

namespace cgo::impl {

using Fd = size_t;

class Event {
 public:
  static constexpr size_t IN = 0x1;
  static constexpr size_t OUT = 0x2;
  static constexpr size_t ERR = 0x4;
  static constexpr size_t ONESHOT = 0x8;

 public:
  Event() = default;
  Event(size_t events) : _events(events) {}
  operator size_t() { return this->_events; }

#if defined(linux) || defined(__linux) || defined(__linux__)

 public:
  static auto from_linux(size_t linux_event) -> Event;
  static auto to_linux(Event cgo_event) -> size_t;

#endif

 private:
  size_t _events = 0;
};

class EventHandler {
 public:
  EventHandler(size_t fd_capacity);
  EventHandler(const EventHandler&) = delete;
  ~EventHandler();
  auto add(Fd fd, Event on) -> Channel<Event>;
  void mod(Fd fd, Event on);
  void del(Fd fd);
  auto handle(size_t handle_batch = 128, size_t timeout_ms = 50) -> size_t;

 private:
  Fd _handler_fd;
  SpinLock _mtx;
  std::unordered_map<Fd, Channel<Event>> _fd_chan;
};

}  // namespace cgo::impl
