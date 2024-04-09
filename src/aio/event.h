#pragma once

#include <functional>
#include <map>

#include "util/slock.h"

namespace cgo::_impl {

using Fd = int;

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
  Event operator|(Event rhs) { return this->_events | rhs._events; }
  Event operator|=(Event rhs) { return this->_events |= rhs._events; }

#if defined(linux) || defined(__linux) || defined(__linux__)
 public:
  static Event from_linux(size_t linux_event);
  static size_t to_linux(Event cgo_event);
#endif

 private:
  size_t _events = 0;
};

class EventHandler {
 public:
  EventHandler();

  void add(Fd fd, Event ev, std::function<void(Event)>&& callback);
  void mod(Fd fd, Event ev);
  void del(Fd fd);
  auto handle(size_t handle_batch = 128, size_t timeout_ms = 50) -> size_t;

 public:
  static thread_local EventHandler* current;

 private:
  util::SpinLock _mutex;
  std::unordered_map<Fd, std::function<void(Event)>> _callbacks;
  Fd _handler_fd = 0;
};

}  // namespace cgo::_impl

namespace cgo {}  // namespace cgo
