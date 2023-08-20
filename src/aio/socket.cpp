#include "aio/socket.h"

#include "aio/error.h"

// #define USE_DEBUG
#include "util/format.h"
#include "util/log.h"

#if defined(linux) || defined(__linux) || defined(__linux__)
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace cgo::impl {

const size_t SocketBufferSize = 128;

auto set_fd_nonblock(Fd fd) -> int {
  int flags = ::fcntl(fd, F_GETFL);
  flags |= O_NONBLOCK;
  return ::fcntl(fd, F_SETFL, flags);
}

Socket::Socket() : Socket(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) {}

Socket::Socket(Fd fd) : _fd(fd), _chan(1) {
  if (int(this->_fd) < 0) {
    throw AioException(format("socket{fd=%d} create fail", this->_fd));
  }
  if (set_fd_nonblock(this->_fd) < 0) {
    throw AioException(format("socket{fd=%d} create set nonblock fail", this->_fd));
  }
  Context::current().handler().add(this->_fd, 0, this->_chan);
  DEBUG("socket{fd=%d} created", this->_fd);
}

Socket::~Socket() {}

auto Socket::recv(size_t size) -> Async<std::string> {
  std::string res(size, '\n');
  while (true) {
    int n = ::recv(this->_fd, res.data(), res.size(), 0);
    if (n > 0) {
      DEBUG("socket{fd=%d} recv %d bytes", this->_fd, n);
      res.resize(n);
      co_return std::move(res);
    }
    if (errno = EAGAIN) {
      Context::current().handler().mod(this->_fd, Event::IN | Event::ONESHOT);
      if ((co_await this->_chan.recv()) & Event::ERR) {
        throw AioException(format("socket{fd=%d} recv fail", this->_fd));
      }
    } else {
      throw AioException(format("socket{fd=%d} recv fail", this->_fd));
    }
  }
}

auto Socket::send(const std::string& data) -> Async<void> {
  for (int i = 0; i < data.size();) {
    int n = ::send(this->_fd, data.data() + i, std::min(SocketBufferSize, data.size() - i), 0);
    if (n > 0) {
      DEBUG("socket{fd=%d} send %d bytes", this->_fd, n);
      i += n;
      continue;
    }
    if (errno = EAGAIN) {
      Context::current().handler().mod(this->_fd, Event::OUT | Event::ONESHOT);
      if ((co_await this->_chan.recv()) & Event::ERR) {
        throw AioException(format("socket{fd=%d} send fail", this->_fd));
      }
    } else {
      throw AioException(format("socket{fd=%d} send fail", this->_fd));
    }
  }
}

void Socket::bind(size_t port) {
  ::sockaddr_in saddr;
  saddr.sin_family = AF_INET;
  saddr.sin_port = htons(port);
  saddr.sin_addr.s_addr = INADDR_ANY;  // 0 = 0.0.0.0
  int optval = 1;
  if (::setsockopt(this->_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
    throw AioException(format("socket{fd=%d} set addr reuse fail", this->_fd));
  }
  if (::bind(this->_fd, (struct ::sockaddr*)&saddr, sizeof(saddr)) < 0) {
    throw AioException(format("socket{fd=%d} bind addr fail", this->_fd));
  }
}

void Socket::listen(size_t backlog) {
  if (::listen(this->_fd, backlog) < 0) {
    throw AioException(format("socket{fd=%d} listen fail", this->_fd));
  }
}

auto Socket::accept() -> Async<Socket> {
  ::sockaddr_in caddr = {};
  ::socklen_t sin_size = sizeof(::sockaddr_in);
  while (true) {
    int fd = ::accept(this->_fd, (::sockaddr*)&caddr, &sin_size);
    if (fd > 0) {
      DEBUG("socket{fd=%d} accept %d", this->_fd, fd);
      co_return Socket(fd);
    }
    if (errno == EAGAIN) {
      Context::current().handler().mod(this->_fd, Event::IN | Event::ONESHOT);
      if ((co_await this->_chan.recv()) & Event::ERR) {
        throw AioException(format("socket{fd=%d} accept fail", this->_fd));
      }
    } else {
      throw AioException(format("socket{fd=%d} accept fail", this->_fd));
    }
  }
}

auto Socket::connect(const char* ip, size_t port) -> Async<void> {
  ::sockaddr_in saddr;
  saddr.sin_family = AF_INET;
  saddr.sin_port = htons(port);
  inet_pton(AF_INET, ip, &saddr.sin_addr.s_addr);
  while (true) {
    if (::connect(this->_fd, (::sockaddr*)&saddr, sizeof(saddr))) {
      DEBUG("socket{fd=%d} connect to %s:%d", this->_fd, ip, port);
      co_return;
    }
    if (errno == EINPROGRESS) {
      DEBUG("socket{fd=%d} connect inprogres", this->_fd);
      Context::current().handler().mod(this->_fd, Event::OUT | Event::ONESHOT);
      if ((co_await this->_chan.recv()) & Event::ERR) {
        throw AioException(format("socket{fd=%d} connect fail", this->_fd));
      }
      int conn_status = 0;
      ::socklen_t len = sizeof(conn_status);
      if (::getsockopt(this->_fd, SOL_SOCKET, SO_ERROR, &conn_status, &len) < 0 || conn_status != 0) {
        throw AioException(format("socket{fd=%d} connect fail", this->_fd));
      }

    } else {
      throw AioException(format("socket{fd=%d} connect fail", this->_fd));
    }
  }
}

void Socket::close() {
  Context::current().handler().del(this->_fd);
  ::close(this->_fd);
  DEBUG("socket{%d} close", this->_fd);
}

}  // namespace cgo::impl

#endif
