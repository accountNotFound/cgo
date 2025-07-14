#pragma once

#include <expected>
#include <functional>

#include "core/schedule.h"

namespace cgo::_impl {

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

class EventContext {
 public:
  struct Handler {
    int fd;
    Event on;
    std::function<void(Event)> fn;
  };

  class Dispatcher {
   public:
    Dispatcher();

    ~Dispatcher();

    void add(int fd, Event ev, std::function<void(Event)>&& callback);

    void mod(int fd, Event ev, std::function<void(Event)>&& callback);

    void del(int fd);

    size_t handle(size_t handle_batch = 128, size_t timeout_ms = 50);

   private:
    Spinlock _mtx;
    std::unordered_map<int, Handler> _hid_calls;
    std::unordered_map<int, int> _fd_hids;
    std::atomic<int> _gid = 0;
    int _fd = 0;
  };

  static auto at(Context& ctx) -> EventContext&;

  EventContext(size_t n_partition) : _dispatchers(n_partition) {}

  void add(int fd, Event ev, std::function<void(Event)>&& callback) {
    handler(fd).add(fd, ev, std::forward<std::function<void(Event)>>(callback));
  }

  void mod(int fd, Event ev, std::function<void(Event)>&& callback) {
    handler(fd).mod(fd, ev, std::forward<std::function<void(Event)>>(callback));
  }

  void del(int fd) { handler(fd).del(fd); }

  size_t run_handler(size_t pindex, size_t batch_size, std::chrono::duration<double, std::milli> timeout) {
    return handler(pindex).handle(batch_size, timeout.count());
  }

  auto handler(int i) -> Dispatcher& { return _dispatchers[i % _dispatchers.size()]; }

 private:
  std::vector<Dispatcher> _dispatchers;
};

class EventLazySignal : public BaseLazySignal {
 public:
  EventLazySignal(Context& ctx, size_t pindex, bool nowait = false);

  void close();

 private:
  Context* _ctx;
  size_t _pindex;
  bool _nowait;
  int _fd;
  int _sig_data = 0;

  void _emit() override;

  void _wait(std::unique_lock<Spinlock>& guard, std::chrono::duration<double, std::milli> timeout) override;

  void _callback(Event ev);
};

}  // namespace cgo::_impl

namespace cgo {

/**
 * @brief Only support IPV4 now
 *
 * @note Default constructor will just return a null socket. You may always call `Socket::create()`
 *
 * Don't use socket across context
 */
class Socket {
 public:
  enum class Protocol {
    TCP,
    UDP,
    ICMP,  // not support now
    SCTP   // not support now
  };

  enum class AddressFamily {
    IPv4,
    IPv6  // not support now
  };

  struct Error {
    int fd = -1;
    int err_code = 0;
    std::string err_msg = "";

    operator bool() const { return fd > 0; }
  };

  static auto create(Context& ctx, Protocol protocol, AddressFamily family) -> Socket {
    return Socket(ctx, protocol, family);
  }

  Socket() = default;

  std::expected<void, Error> bind(const std::string& ip, uint16_t port);

  std::expected<void, Error> listen(size_t backlog = 1024);

  Coroutine<std::expected<Socket, Error>> accept();

  /**
   * @brief Accept a socket and bind to given context
   */
  Coroutine<std::expected<Socket, Error>> accept(Context& ctx);

  Coroutine<std::expected<void, Error>> connect(
      const std::string& ip, uint16_t port,
      std::chrono::duration<double, std::milli> timeout = std::chrono::duration<double, std::milli>(-1));

  Coroutine<std::expected<std::string, Error>> recv(
      size_t size, std::chrono::duration<double, std::milli> timeout = std::chrono::duration<double, std::milli>(-1));

  Coroutine<std::expected<void, Error>> send(
      const std::string& data,
      std::chrono::duration<double, std::milli> timeout = std::chrono::duration<double, std::milli>(-1));

  Coroutine<std::expected<size_t, Error>> send_to(const std::string& data, const std::string& ip, uint16_t port,
                                                  std::chrono::milliseconds timeout = std::chrono::milliseconds(-1));

  Coroutine<std::expected<std::pair<std::string, std::pair<std::string, uint16_t>>, Error>> recv_from(
      size_t size, std::chrono::milliseconds timeout = std::chrono::milliseconds(-1));

  void close();

  int fd() const { return _fd; }

  operator int() const { return _fd; }

  operator bool() const { return bool(_ctx); }

 private:
  Context* _ctx = nullptr;
  int _fd = -1;
  Protocol _protocol;
  AddressFamily _family;

  Socket(Context& ctx, Protocol protocol, AddressFamily family);

  Socket(Context& ctx, int fd, Protocol protocol, AddressFamily family);

  void _set_sock_opt();
};

}  // namespace cgo
