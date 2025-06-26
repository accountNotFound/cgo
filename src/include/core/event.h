#pragma once

#include "core/schedule.h"

namespace cgo::_impl::_event {

using Fd = int;

class Event {
 public:
  static constexpr size_t IN = 0x1;
  static constexpr size_t OUT = 0x2;
  static constexpr size_t ERR = 0x4;
  static constexpr size_t ONESHOT = 0x8;

 public:
  Event() : _events(0) {}

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
  size_t _events;
};

class EventDispatcher;

class EventSignal : public SignalBase {
 public:
  EventSignal(size_t p_index, size_t ev_batch_size);

  void close();

  void emit() override;

  void wait(const std::chrono::duration<double, std::milli>& timeout) override;

  void wait_event_proc(const std::chrono::duration<double, std::milli>& timeout) { this->wait(timeout); }

 private:
  int _p_index;
  int _ev_batch;
  int _fd;
};

class EventHandler {
 public:
  EventHandler();

  ~EventHandler();

  void add(Fd fd, Event ev, std::function<void(Event)>&& callback);

  void mod(Fd fd, Event ev);

  void del(Fd fd);

  size_t handle(size_t handle_batch = 128, size_t timeout_ms = 50);

 private:
  Spinlock _mtx;
  std::unordered_map<Fd, std::function<void(Event)>> _fd_calls;
  Fd _fd = 0;
};

class EventDispatcher {
 public:
  EventDispatcher(size_t n_partition) : _handlers(n_partition) {}

  void add(Fd fd, Event ev, std::function<void(Event)>&& callback) { this->_loc(fd).add(fd, ev, std::forward<decltype(callback)>(callback)); }

  void mod(Fd fd, Event ev) { this->_loc(fd).mod(fd, ev); }

  void del(Fd fd) { this->_loc(fd).del(fd); }

  size_t handle(size_t p_index, size_t batch_size, const std::chrono::duration<double, std::milli>& timeout) {
    return this->_loc(p_index).handle(batch_size, timeout.count());
  }

 private:
  std::vector<EventHandler> _handlers;

  EventHandler& _loc(Fd fd) { return this->_handlers[fd % this->_handlers.size()]; }
};

inline std::unique_ptr<EventDispatcher> g_dispatcher = nullptr;

inline EventDispatcher& get_dispatcher() { return *g_dispatcher; }

}  // namespace cgo::_impl::_event
