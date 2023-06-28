#pragma once

#include <string>

#include "core/event.h"

#if defined(linux) || defined(__linux) || defined(__linux__)

#include <arpa/inet.h>
#include <sys/socket.h>

#endif

namespace cgo::impl {

class Connection {
 public:
  Connection(Fd fd);
  ~Connection();
  auto recv() -> Async<std::string>;
  auto send(const std::string& data) -> Async<void>;

 private:
  class Impl;
  std::shared_ptr<Impl> _impl;
};

class TcpServer {
 public:
  TcpServer(size_t port);
  TcpServer(const TcpServer&) = delete;
  ~TcpServer();
  auto accept() -> Async<Connection>;

 private:
  Fd _fd;
  Channel<Event> _chan;

#if defined(linux) || defined(__linux) || defined(__linux__)

 private:
  ::sockaddr_in _svr_addr;

#endif
};

class TcpClient {
 public:
  TcpClient(const std::string& host, size_t port);
  TcpClient(const TcpClient&) = delete;
  ~TcpClient();
  auto connect() -> Async<Connection>;

 private:
  Channel<Event> _chan;

#if defined(linux) || defined(__linux) || defined(__linux__)

 private:
  ::sockaddr_in _svr_addr;

#endif
};

}  // namespace cgo::impl