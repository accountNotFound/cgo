#pragma once

#include "./event.h"
#include "core/condition.h"

namespace cgo {

// only TCP and IPV4 are supported now. You should explicitly call `close()` if you don't use the socket anymore
class Socket {
 public:
  Socket();

  Coroutine<std::string> recv(size_t size);
  Coroutine<void> send(const std::string& data);
  void close();

  Coroutine<void> connect(const char* ip, size_t port);
  void bind(size_t port);
  void listen(size_t backlog = 1024);
  Coroutine<Socket> accept();

  _impl::Fd fileno() const { return this->_fd; }

 private:
  Socket(_impl::Fd fd);

 private:
  _impl::Fd _fd;
  Channel<_impl::Event> _chan;
};

class SocketException : public std::exception {
 public:
  SocketException(_impl::Fd fd, const std::string& msg);
  const char* what() const noexcept { return this->_msg.data(); }
  int errcode() const { return this->_code; }

 private:
  std::string _msg;
  int _code;
  _impl::Fd _fd;
};

}  // namespace cgo
