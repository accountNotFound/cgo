#include "core/event.h"
#include "core/timed.h"

#if defined(linux) || defined(__linux) || defined(__linux__)

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>

#endif

namespace cgo {

using _impl::_event::Event;

/**
 * @return True if event activated, or False if timeout
 */
Coroutine<bool> wait_event(
    Context& ctx, int fd, Event on,
    std::chrono::duration<double, std::milli> timeout = std::chrono::duration<double, std::milli>(-1)) {
  if (timeout.count() < 0) {
    Semaphore sem(0);
    _impl::_event::get_dispatcher().mod(fd, on, [&sem](Event) { sem.release(); });
    co_await sem.aquire();
    co_return true;
  } else {
    struct Signal {
      std::atomic<int> timeout = 0;
      Semaphore sem = {0};
    };

    auto s = std::make_shared<Signal>(0);
    _impl::_event::get_dispatcher().mod(fd, on, [s](Event) {
      int expected = 0;
      if (s->timeout.compare_exchange_weak(expected, 1)) {
        s->sem.release();
      }
    });
    _impl::TimedContext::at(ctx).create_timeout(
        [s]() {
          int expected = 0;
          if (s->timeout.compare_exchange_weak(expected, -1)) {
            s->sem.release();
          }
        },
        timeout);
    co_await s->sem.aquire();
    co_return (s->timeout == 1);
  }
}

#if defined(linux) || defined(__linux) || defined(__linux__)

Socket::Socket(Context& ctx) : Socket(ctx, ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) {}

Socket::Socket(Context& ctx, int fd) : _ctx(&ctx), _fd(fd) {
  int flags = ::fcntl(this->_fd, F_GETFL) | O_NONBLOCK;
  if (::fcntl(this->_fd, F_SETFL, flags) < 0) {
    throw Socket::Error(this->_fd, ::strerror(errno));
  }
  _impl::_event::get_dispatcher().add(this->_fd, Event::ERR | Event::ONESHOT, [](_impl::_event::Event) {});
}

std::expected<void, Socket::Error> Socket::bind(size_t port) {
  ::sockaddr_in saddr;
  saddr.sin_family = AF_INET;
  saddr.sin_port = ::htons(port);
  saddr.sin_addr.s_addr = INADDR_ANY;  // 0 = 0.0.0.0
  int optval = 1;
  if (::setsockopt(this->_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
    return std::unexpected(Error(this->_fd, ::strerror(errno)));
  }
  if (::bind(this->_fd, (::sockaddr*)&saddr, sizeof(saddr)) < 0) {
    return std::unexpected(Error(this->_fd, ::strerror(errno)));
  }
  return {};
}

std::expected<void, Socket::Error> Socket::listen(size_t backlog) {
  if (::listen(this->_fd, backlog) < 0) {
    return std::unexpected(Error(this->_fd, ::strerror(errno)));
  }
  return {};
}

Coroutine<std::expected<Socket, Socket::Error>> Socket::accept() {
  ::sockaddr_in caddr = {};
  ::socklen_t sin_size = sizeof(::sockaddr_in);
  while (true) {
    int fd = ::accept(this->_fd, (::sockaddr*)&caddr, &sin_size);
    if (fd > 0) {
      co_return Socket(*this->_ctx, fd);
    }
    if (errno != EAGAIN) {
      co_return std::unexpected(Error(this->_fd, ::strerror(errno)));
    }
    co_await wait_event(*this->_ctx, this->_fd, Event::IN | Event::ONESHOT);
  }
}

Coroutine<std::expected<void, Socket::Error>> Socket::connect(const std::string& ip, size_t port,
                                                              std::chrono::duration<double, std::milli> timeout) {
  ::sockaddr_in saddr;
  saddr.sin_family = AF_INET;
  saddr.sin_port = htons(port);
  inet_pton(AF_INET, ip.data(), &saddr.sin_addr.s_addr);
  if (::connect(this->_fd, (::sockaddr*)&saddr, sizeof(saddr)) == 0) {
    co_return {};
  }
  if (errno != EINPROGRESS) {
    co_return std::unexpected(Error(this->_fd, ::strerror(errno)));
  }
  if (!(co_await wait_event(*this->_ctx, this->_fd, Event::OUT | Event::ONESHOT, timeout))) {
    co_return std::unexpected(Error(this->_fd, "connect timeout"));
  }

  int conn_status = 0;
  ::socklen_t len = sizeof(conn_status);
  if (::getsockopt(this->_fd, SOL_SOCKET, SO_ERROR, &conn_status, &len) < 0) {
    co_return std::unexpected(Error(this->_fd, ::strerror(errno)));
  }
  if (conn_status != 0) {
    co_return std::unexpected(Error(this->_fd, ::strerror(conn_status)));
  }
  co_return {};
}

Coroutine<std::expected<std::string, Socket::Error>> Socket::recv(size_t size,
                                                                  std::chrono::duration<double, std::milli> timeout) {
  std::string res(size, '\0');
  while (true) {
    int n = ::recv(this->_fd, res.data(), res.size(), 0);
    if (n > 0) {
      co_return res.substr(0, n);
    }
    if (n == 0) {
      co_return std::unexpected(Error(this->_fd, "close by other side"));
    }
    if (errno != EAGAIN) {
      co_return std::unexpected(Error(this->_fd, ::strerror(errno)));
    }
    if (!(co_await wait_event(*this->_ctx, this->_fd, Event::IN | Event::ONESHOT, timeout))) {
      co_return std::unexpected(Error(this->_fd, "recv timeout"));
    }
  }
}

Coroutine<std::expected<void, Socket::Error>> Socket::send(const std::string& data,
                                                           std::chrono::duration<double, std::milli> timeout) {
  for (int i = 0; i < data.size();) {
    int n = ::send(this->_fd, data.data() + i, data.size() - i, 0);
    if (n > 0) {
      i += n;
      continue;
    }
    if (n == 0) {
      co_return std::unexpected(Error(this->_fd, "close by other side"));
    }
    if (errno != EAGAIN) {
      co_return std::unexpected(Error(this->_fd, ::strerror(errno)));
    }
    if (!(co_await wait_event(*this->_ctx, this->_fd, Event::OUT | Event::ONESHOT, timeout))) {
      co_return std::unexpected(Error(this->_fd, "recv timeout"));
    }
  }
}

void Socket::close() {
  _impl::_event::get_dispatcher().del(this->_fd);
  ::close(this->_fd);
}

#endif

}  // namespace cgo
