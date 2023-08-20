#pragma once

#include "core/event.h"

namespace cgo::impl {

// for now only TCP and IPV4 are supported
class Socket {
 public:
  Socket();
  Socket(Fd fd);
  ~Socket();

  auto recv(size_t size) -> Async<std::string>;
  auto send(const std::string& data) -> Async<void>;
  void close();

  auto connect(const char* ip, size_t port) -> Async<void>;
  void bind(size_t port);
  void listen(size_t backlog = 5);
  auto accept() -> Async<Socket>;

  auto fileno() const -> Fd { return this->_fd; }

 private:
  Fd _fd;
  Channel<Event> _chan;
};

}  // namespace cgo::impl