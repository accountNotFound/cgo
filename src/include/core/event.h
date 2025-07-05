#pragma once

#include <expected>
#include <functional>

#include "core/schedule.h"

namespace cgo::_impl::_event {

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

class EventHandler {
 public:
  EventHandler();

  ~EventHandler();

  void add(int fd, Event ev, std::function<void(Event)>&& callback);

  void mod(int fd, Event ev, std::function<void(Event)>&& callback);

  void del(int fd);

  size_t handle(size_t handle_batch = 128, size_t timeout_ms = 50);

 private:
  struct Callback {
    int fd;
    Event on;
    std::function<void(Event)> fn;
  };

  Spinlock _mtx;
  std::unordered_map<int, Callback> _m_calls;
  std::unordered_map<int, int> _fd_cids;
  std::atomic<int> _tid = 0;
  int _fd = 0;
};

class EventDispatcher {
 public:
  EventDispatcher(size_t n_partition) : _handlers(n_partition) {}

  void add(int fd, Event ev, std::function<void(Event)>&& fn) {
    this->_loc(fd).add(fd, ev, std::forward<decltype(fn)>(fn));
  }

  void mod(int fd, Event ev, std::function<void(Event)>&& fn) {
    this->_loc(fd).mod(fd, ev, std::forward<decltype(fn)>(fn));
  }

  void del(int fd) { this->_loc(fd).del(fd); }

  size_t handle(size_t p_index, size_t batch_size, std::chrono::duration<double, std::milli> timeout) {
    return this->_loc(p_index).handle(batch_size, timeout.count());
  }

 private:
  std::vector<EventHandler> _handlers;

  EventHandler& _loc(int fd) { return this->_handlers[fd % this->_handlers.size()]; }
};

inline std::unique_ptr<EventDispatcher> g_dispatcher = nullptr;

inline EventDispatcher& get_dispatcher() { return *g_dispatcher; }

}  // namespace cgo::_impl::_event

namespace cgo::_impl {

class EventSignal : public LazySignalBase {
 public:
  EventSignal();

  void close();

  void emit() override;

  void wait(std::chrono::duration<double, std::milli> timeout) override;

 private:
  int _fd;
  int _event_flag = 0;

  void _callback(_event::Event ev);
};

}  // namespace cgo::_impl

namespace cgo {

/**
 * @brief Only support TCP/IPV4 now
 */
class Socket {
 public:
  struct Error {
    int fd = -1;
    std::string msg = "";

    operator bool() const { return fd > 0; }
  };

  Socket();

  std::expected<void, Error> bind(size_t port);

  std::expected<void, Error> listen(size_t backlog = 1024);

  Coroutine<std::expected<Socket, Error>> accept();

  Coroutine<std::expected<void, Error>> connect(
      const std::string& ip, size_t port,
      std::chrono::duration<double, std::milli> timeout = std::chrono::duration<double, std::milli>(-1));

  Coroutine<std::expected<std::string, Error>> recv(
      size_t size, std::chrono::duration<double, std::milli> timeout = std::chrono::duration<double, std::milli>(-1));

  Coroutine<std::expected<void, Error>> send(
      const std::string& data,
      std::chrono::duration<double, std::milli> timeout = std::chrono::duration<double, std::milli>(-1));

  void close();

  int fd() const { return this->_fd; }

  operator int() const { return this->_fd; }

 private:
  int _fd;

  Socket(int fd);
};

}  // namespace cgo
