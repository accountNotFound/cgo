#include "./socket.h"

#include "util/format.h"

#if defined(linux) || defined(__linux) || defined(__linux__)
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace cgo {

using _impl::Event;

SocketException::SocketException(_impl::Fd fd, const std::string& msg)
#if defined(linux) || defined(__linux) || defined(__linux__)
    : _code(errno),
      _fd(fd),
      _msg(util::format("[fd=%d, errno=%d], %s", fd, errno, msg.data()))
#endif
{
}

Socket::Socket()
    : Socket(
#if defined(linux) || defined(__linux) || defined(__linux__)
          ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)
#endif
      ) {
}

Socket::Socket(_impl::Fd fd) : _chan(INT_MAX), _fd(fd) {
  if (this->_fd < 0) {
    throw SocketException(this->_fd, "create socket fail");
  }

  int flags = ::fcntl(this->_fd, F_GETFL) | O_NONBLOCK;
  if (::fcntl(this->_fd, F_SETFL, flags) < 0) {
    throw SocketException(this->_fd, "set nonblock fail");
  }
}

Coroutine<std::string> Socket::recv(size_t size) {
#if defined(linux) || defined(__linux) || defined(__linux__)
  std::string res(size, '\0');
  while (true) {
    int n = ::recv(this->_fd, res.data(), res.size(), 0);
    if (n > 0) {
      res.resize(n);
      co_return std::move(res);
    } else if (n == 0) {
      throw SocketException(this->_fd, "close by other side in recv");
    }
    // n<0
    if (errno == EAGAIN) {
      Event ev = co_await this->_chan.recv();
      if (ev & Event::ERR) {
        throw SocketException(this->_fd, "recv fail");
      }
    } else {
      throw SocketException(this->_fd, "recv fail");
    }
  }
#endif
}

Coroutine<void> Socket::send(const std::string& data) {
#if defined(linux) || defined(__linux) || defined(__linux__)
  for (int i = 0; i < data.size();) {
    int n = ::send(this->_fd, data.data() + i, data.size() - i, 0);
    if (n > 0) {
      i += n;
      continue;
    }
    if (n == 0) {
      throw SocketException(this->_fd, "close by other side in send");
    }
    if (errno = EAGAIN) {
      _impl::EventHandler::current->mod(this->_fd, Event::OUT | Event::ONESHOT);
      Event ev = co_await this->_chan.recv();
      _impl::EventHandler::current->mod(this->_fd, Event::IN);
      if (ev & Event::ERR) {
        throw SocketException(this->_fd, "send fail");
      }
    } else {
      throw SocketException(this->_fd, "send fail");
    }
  }
#endif
}

void Socket::bind(size_t port) {
#if defined(linux) || defined(__linux) || defined(__linux__)
  ::sockaddr_in saddr;
  saddr.sin_family = AF_INET;
  saddr.sin_port = htons(port);
  saddr.sin_addr.s_addr = INADDR_ANY;  // 0 = 0.0.0.0
  int optval = 1;
  if (::setsockopt(this->_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
    throw SocketException(this->_fd, "set addr reuse fail");
  }
  if (::bind(this->_fd, (struct ::sockaddr*)&saddr, sizeof(saddr)) < 0) {
    throw SocketException(this->_fd, ("bind addr fail"));
  }
#endif
}

void Socket::listen(size_t backlog) {
#if defined(linux) || defined(__linux) || defined(__linux__)
  if (::listen(this->_fd, backlog) < 0) {
    throw SocketException(this->_fd, "listen fail");
  }
#endif
  _impl::EventHandler::current->add(this->_fd, Event::IN,
                                    [chan = this->_chan](Event ev) mutable { chan.send_nowait(std::move(ev)); });
}

Coroutine<Socket> Socket::accept() {
#if defined(linux) || defined(__linux) || defined(__linux__)
  ::sockaddr_in caddr = {};
  ::socklen_t sin_size = sizeof(::sockaddr_in);
  while (true) {
    int fd = ::accept(this->_fd, (::sockaddr*)&caddr, &sin_size);
    if (fd > 0) {
      Socket sock(fd);
      _impl::EventHandler::current->add(fd, Event::IN,
                                        [chan = sock._chan](Event ev) mutable { chan.send_nowait(std::move(ev)); });
      co_return std::move(sock);
    }
    if (errno == EAGAIN) {
      Event ev = co_await this->_chan.recv();
      if (ev & Event::ERR) {
        throw SocketException(this->_fd, "accept fail");
      }
    } else {
      throw SocketException(this->_fd, "accept fail");
    }
  }
#endif
}

Coroutine<void> Socket::connect(const char* ip, size_t port) {
#if defined(linux) || defined(__linux) || defined(__linux__)
  ::sockaddr_in saddr;
  saddr.sin_family = AF_INET;
  saddr.sin_port = htons(port);
  inet_pton(AF_INET, ip, &saddr.sin_addr.s_addr);
  while (true) {
    if (::connect(this->_fd, (::sockaddr*)&saddr, sizeof(saddr)) == 0) {
      co_return;
    }
    if (errno == EINPROGRESS) {
      _impl::EventHandler::current->add(this->_fd, Event::OUT,
                                        [chan = this->_chan](Event ev) mutable { chan.send_nowait(std::move(ev)); });
      Event ev = co_await this->_chan.recv();
      if (ev & Event::ERR) {
        throw SocketException(this->_fd, "connect fail");
      }
      int conn_status = 0;
      ::socklen_t len = sizeof(conn_status);
      if (::getsockopt(this->_fd, SOL_SOCKET, SO_ERROR, &conn_status, &len) < 0 || conn_status != 0) {
        throw SocketException(this->_fd, "connect fail");
      }
      _impl::EventHandler::current->mod(this->_fd, Event::IN);
      co_return;
    } else {
      throw SocketException(this->_fd, "connect fail");
    }
  }
#endif
}

void Socket::close() {
  if (this->_fd > 0) {
    _impl::EventHandler::current->del(this->_fd);
    ::close(this->_fd);
    this->_fd = 0;
    // this->_chan.send_nowait(Event::ERR);
  }
}

}  // namespace cgo
