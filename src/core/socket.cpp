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

using _impl::Event;

Coroutine<bool> Socket::_wait_sock_event(Event on, std::chrono::duration<double, std::milli> timeout) {
  struct Signal {
    std::atomic<int> timeout = 0;
    Semaphore signal = {0};
  };

  auto s = std::make_shared<Signal>(0);
  _impl::EventContext::at(*_ctx).mod(_fd, on, [s](Event) {
    int expected = 0;
    if (s->timeout.compare_exchange_weak(expected, 1)) {
      s->signal.release();
    }
  });
  if (timeout.count() > 0) {
    _impl::TimedContext::at(*_ctx).create_timeout(
        [s]() {
          int expected = 0;
          if (s->timeout.compare_exchange_weak(expected, -1)) {
            s->signal.release();
          }
        },
        timeout);
  }
  co_await s->signal.aquire();
  co_return (s->timeout == 1);
}

#if defined(linux) || defined(__linux) || defined(__linux__)

Socket::Socket(Context& ctx, Socket::Protocol protocol, Socket::AddressFamily family)
    : _ctx(&ctx), _protocol(protocol), _family(family) {
  // TODO: IPV6 not support now
  int type = (protocol == Protocol::TCP) ? SOCK_STREAM : SOCK_DGRAM;
  int domain = (family == AddressFamily::IPv4) ? AF_INET : AF_INET6;

  _fd = ::socket(domain, type, 0);
  if (_fd < 0) {
    throw Socket::Error(_fd, errno, ::strerror(errno));
  }
  _set_sock_opt();
}

Socket::Socket(Context& ctx, int fd, Protocol protocol, AddressFamily family)
    : _ctx(&ctx), _fd(fd), _protocol(protocol), _family(family) {
  _set_sock_opt();
}

std::expected<void, Socket::Error> Socket::bind(const std::string& ip, uint16_t port) {
  if (_family == AddressFamily::IPv4) {
    ::sockaddr_in saddr{};
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(port);

    if (ip.empty() || ip == "0.0.0.0") {
      saddr.sin_addr.s_addr = INADDR_ANY;
    } else if (inet_pton(AF_INET, ip.c_str(), &saddr.sin_addr) <= 0) {
      return std::unexpected(Error(_fd, errno, "Invalid IPv4 address"));
    }

    if (::bind(_fd, (sockaddr*)&saddr, sizeof(saddr)) < 0) {
      return std::unexpected(Error(_fd, errno, ::strerror(errno)));
    }
  } else {
    // TODO: IPV6 not support now
  }
  return {};
}

std::expected<void, Socket::Error> Socket::listen(size_t backlog) {
  if (_protocol != Protocol::TCP) {
    return std::unexpected(Error(_fd, 0, "Listen only supported for TCP sockets"));
  }
  if (::listen(_fd, backlog) < 0) {
    return std::unexpected(Error(_fd, errno, ::strerror(errno)));
  }
  return {};
}

Coroutine<std::expected<Socket, Socket::Error>> Socket::accept() { return accept(*_ctx); }

Coroutine<std::expected<Socket, Socket::Error>> Socket::accept(Context& ctx) {
  if (_protocol != Protocol::TCP) {
    co_return std::unexpected(Error(_fd, 0, "Accept only supported for TCP sockets"));
  }
  ::sockaddr_in caddr = {};
  socklen_t sin_size = sizeof(caddr);
  while (true) {
    int fd = ::accept(_fd, (sockaddr*)&caddr, &sin_size);
    if (fd > 0) {
      co_return Socket(ctx, fd, _protocol, _family);
    }
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
      co_return std::unexpected(Error(_fd, errno, ::strerror(errno)));
    }
    co_await _wait_sock_event(Event::IN | Event::ONESHOT);
  }
}

Coroutine<std::expected<void, Socket::Error>> Socket::connect(const std::string& ip, uint16_t port,
                                                              std::chrono::duration<double, std::milli> timeout) {
  if (_family == AddressFamily::IPv4) {
    ::sockaddr_in saddr{};
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip.c_str(), &saddr.sin_addr) <= 0) {
      co_return std::unexpected(Error(_fd, errno, "Invalid IPv4 address"));
    }

    if (::connect(_fd, (sockaddr*)&saddr, sizeof(saddr)) == 0) {
      co_return {};
    }

    // UDP connect 会立即"成功"，因为UDP是无连接的
    if (_protocol == Protocol::UDP && errno == EINPROGRESS) {
      co_return {};
    }

    if (errno != EINPROGRESS) {
      co_return std::unexpected(Error(_fd, errno, ::strerror(errno)));
    }

    if (!(co_await _wait_sock_event(Event::OUT | Event::ONESHOT, timeout))) {
      co_return std::unexpected(Error(_fd, errno, "connect timeout"));
    }

    int conn_status = 0;
    socklen_t len = sizeof(conn_status);
    if (::getsockopt(_fd, SOL_SOCKET, SO_ERROR, &conn_status, &len) < 0) {
      co_return std::unexpected(Error(_fd, errno, ::strerror(errno)));
    }
    if (conn_status != 0) {
      co_return std::unexpected(Error(_fd, conn_status, ::strerror(conn_status)));
    }
  } else {
    // TODO: IPV6 not support now
  }
  co_return {};
}

Coroutine<std::expected<std::string, Socket::Error>> Socket::recv(size_t size,
                                                                  std::chrono::duration<double, std::milli> timeout) {
  std::string res(size, '\0');
  while (true) {
    int n = (_protocol == Protocol::TCP) ? ::recv(_fd, res.data(), res.size(), 0)
                                         : ::recv(_fd, res.data(), res.size(), MSG_DONTWAIT);

    if (n > 0) {
      res.resize(n);
      co_return res;
    }
    if (n == 0 && _protocol == Protocol::TCP) {
      co_return std::unexpected(Error(_fd, 0, "close by other side"));
    }
    if (n < 0) {
      if (errno == ECONNRESET || errno == EPIPE) {
        co_return std::unexpected(Error(_fd, errno, "close by other side"));
      }
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        co_return std::unexpected(Error(_fd, errno, ::strerror(errno)));
      }
    }
    if (!(co_await _wait_sock_event(Event::IN | Event::ONESHOT, timeout))) {
      co_return std::unexpected(Error(_fd, ETIMEDOUT, "recv timeout"));
    }
  }
}

Coroutine<std::expected<void, Socket::Error>> Socket::send(const std::string& data,
                                                           std::chrono::duration<double, std::milli> timeout) {
  for (size_t i = 0; i < data.size();) {
    int n = (_protocol == Protocol::TCP) ? ::send(_fd, data.data() + i, data.size() - i, MSG_NOSIGNAL)
                                         : ::send(_fd, data.data() + i, data.size() - i, MSG_DONTWAIT | MSG_NOSIGNAL);

    if (n > 0) {
      i += n;
      continue;
    }
    if (n < 0) {
      if (errno == ECONNRESET || errno == EPIPE) {
        co_return std::unexpected(Error(_fd, errno, "close by other side"));
      }
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        co_return std::unexpected(Error(_fd, errno, ::strerror(errno)));
      }
    }
    if (!(co_await _wait_sock_event(Event::OUT | Event::ONESHOT, timeout))) {
      co_return std::unexpected(Error(_fd, ETIMEDOUT, "send timeout"));
    }
  }
  co_return {};
}

Coroutine<std::expected<size_t, Socket::Error>> Socket::sendto(const std::string& data, const std::string& ip,
                                                               uint16_t port, std::chrono::milliseconds timeout) {
  if (_protocol != Protocol::UDP) {
    co_return std::unexpected(Error(_fd, 0, "send_to only supported for UDP sockets"));
  }

  ::sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);

  if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) <= 0) {
    co_return std::unexpected(Error(_fd, errno, "Invalid IPv4 address"));
  }

  while (true) {
    int n = ::sendto(_fd, data.data(), data.size(), MSG_DONTWAIT | MSG_NOSIGNAL, (sockaddr*)&addr, sizeof(addr));

    if (n >= 0) {
      co_return static_cast<size_t>(n);
    }
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
      co_return std::unexpected(Error(_fd, errno, ::strerror(errno)));
    }
    if (!(co_await _wait_sock_event(Event::OUT | Event::ONESHOT, timeout))) {
      co_return std::unexpected(Error(_fd, ETIMEDOUT, "send_to timeout"));
    }
  }
}

Coroutine<std::expected<std::pair<std::string, std::pair<std::string, uint16_t>>, Socket::Error>> Socket::recvfrom(
    size_t size, std::chrono::milliseconds timeout) {
  if (_protocol != Protocol::UDP) {
    co_return std::unexpected(Error(_fd, 0, "recv_from only supported for UDP sockets"));
  }

  std::string buffer(size, '\0');
  ::sockaddr_in addr{};
  socklen_t addr_len = sizeof(addr);

  while (true) {
    int n = ::recvfrom(_fd, buffer.data(), buffer.size(), MSG_DONTWAIT, (sockaddr*)&addr, &addr_len);

    if (n > 0) {
      char ip_str[INET_ADDRSTRLEN];
      inet_ntop(AF_INET, &addr.sin_addr, ip_str, sizeof(ip_str));

      buffer.resize(n);
      auto source = std::make_pair(std::string(ip_str), ntohs(addr.sin_port));
      co_return std::make_pair(buffer, source);
    }
    if (n < 0) {
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        co_return std::unexpected(Error(_fd, errno, ::strerror(errno)));
      }
    }
    if (!(co_await _wait_sock_event(Event::IN | Event::ONESHOT, timeout))) {
      co_return std::unexpected(Error(_fd, ETIMEDOUT, "recv_from timeout"));
    }
  }
}

void Socket::close() {
  _impl::EventContext::at(*_ctx).del(_fd);
  ::close(_fd);
}

void Socket::_set_sock_opt() {
  int flags = ::fcntl(_fd, F_GETFL) | O_NONBLOCK;
  if (::fcntl(_fd, F_SETFL, flags) < 0) {
    throw Socket::Error(_fd, errno, ::strerror(errno));
  }

  // adapt for TCP and UDP
  int optval = 1;
  ::setsockopt(_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

  // 增加接收缓冲区
  int buf_size = 1024 * 1024;  // 1MB
  ::setsockopt(_fd, SOL_SOCKET, SO_RCVBUF, &buf_size, sizeof(buf_size));

  // 使用SO_REUSEPORT
  int reuse = 1;
  ::setsockopt(_fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));

  int enable = 1;
  ::setsockopt(_fd, SOL_SOCKET, SO_ZEROCOPY, &enable, sizeof(enable));

  _impl::EventContext::at(*_ctx).add(_fd, Event::ERR | Event::ONESHOT, [](Event) {});
}

#endif

}  // namespace cgo
