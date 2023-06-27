#include "net.h"

#define USE_DEBUG
#include "util/log.h"

#if defined(linux) || defined(__linux) || defined(__linux__)

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

namespace cgo::impl {

const size_t ListenCapacity = 128;
const size_t BufferSize = 128;

auto set_fd_nonblock(Fd fd) -> int {
  int flags = ::fcntl(fd, F_GETFL);
  flags |= O_NONBLOCK;
  return ::fcntl(fd, F_SETFL, flags);
}

TcpServer::TcpServer(size_t port) {
  this->_svr_addr.sin_family = AF_INET;
  this->_svr_addr.sin_addr.s_addr = ::htonl(INADDR_ANY);
  this->_svr_addr.sin_port = ::htons(port);

  // TODO: error handle
  this->_fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (int(this->_fd) < 0) {
    exit(1);
  }
  if (set_fd_nonblock(this->_fd) < 0) {
    exit(1);
  }
  if (::bind(this->_fd, (::sockaddr*)(&this->_svr_addr), sizeof(this->_svr_addr)) < 0) {
    exit(1);
  }
  if (::listen(this->_fd, ListenCapacity) < 0) {
    exit(1);
  }

  Context::current().handler().add(this->_fd, Event::IN,
                                   [this](Event event) { this->_chan.send_nowait(std::move(event)); });
  DEBUG("tcp server fd=%lu created", this->_fd);
}

TcpServer::~TcpServer() {
  Context::current().handler().del(this->_fd);
  ::close(this->_fd);
}

auto TcpServer::accept() -> Async<Connection> {
  ::sockaddr_in cli_addr = {};
  ::socklen_t sin_size = sizeof(::sockaddr_in);

  while (true) {
    Fd conn_fd = ::accept(this->_fd, (::sockaddr*)(&cli_addr), &sin_size);
    if (int(conn_fd) < 0) {
      co_await this->_chan.recv();
      continue;
    }
    // TODO: error handle
    if (set_fd_nonblock(conn_fd) < 0) {
      exit(1);
    }
    co_return Connection(conn_fd);
  }
}

class Connection::Impl {
 public:
  Impl(Fd fd) : _fd(fd) {
    Context::current().handler().add(this->_fd, this->_ev_listening, [this](Event ev_activate) {
      if (ev_activate & Event::IN) {
        this->_chan_in.send_nowait(std::move(ev_activate));
      } else if (ev_activate & Event::OUT) {
        this->_chan_out.send_nowait(std::move(ev_activate));
      } else {
        // TODO: error handle
      }
    });
    DEBUG("tcp connection fd=%lu created", this->_fd);
  }
  ~Impl() {
    Context::current().handler().del(this->_fd);
    ::close(this->_fd);
    DEBUG("tcp connection fd=%lu closed", this->_fd);
  }
  auto recv() -> Async<std::string> {
    co_await this->_chan_in.recv();
    std::string res;
    while (true) {
      std::string buffer(BufferSize, '\0');
      int nbytes = ::read(this->_fd, buffer.data(), buffer.size());
      DEBUG("copnnection fd=%lu get %d bytes", this->_fd, nbytes);
      if (nbytes <= 0) {
        // TODO: error handle if nbytes==-1
        break;
      } else {
        res += std::move(buffer);
      }
    }
    Context::current().handler().mod(this->_fd, this->_ev_listening);
    co_return std::move(res);
  }
  auto send(const std::string& data) -> Async<void> {
    for (int i = 0; i < data.size(); i += BufferSize) {
      int nbytes = ::write(this->_fd, data.data() + i, std::min(BufferSize, data.size() - i));
      DEBUG("connection fd=%lu send %d bytes", this->_fd, nbytes);
      if (nbytes <= 0) {
        // TODO: error handle if nbytes==-1
        this->_ev_listening = Event::IN | Event::OUT | Event::ONESHOT;
        Context::current().handler().mod(this->_fd, this->_ev_listening);
        co_await this->_chan_out.recv();
        this->_ev_listening = Event::IN | Event::ONESHOT;
      }
    }
  }

 private:
  Fd _fd;
  Channel<Event> _chan_in;
  Channel<Event> _chan_out;
  Event _ev_listening = Event::IN | Event::ONESHOT;
};

Connection::Connection(Fd fd) : _impl(std::make_shared<Connection::Impl>(fd)) {}

Connection::~Connection() = default;

auto Connection::recv() -> Async<std::string> { co_return (co_await this->_impl->recv()); }

auto Connection::send(const std::string& data) -> Async<void> { co_await this->_impl->send(data); }

TcpClient::TcpClient(const std::string& host, size_t port) {
  this->_svr_addr.sin_family = AF_INET;
  this->_svr_addr.sin_addr.s_addr = ::htonl(INADDR_ANY);
  this->_svr_addr.sin_port = ::htons(port);
}

TcpClient::~TcpClient() = default;

auto TcpClient::connect() -> Async<Connection> {
  // TODO: error handle
  Fd conn_fd = ::socket(AF_INET, SOCK_STREAM, 0);
  set_fd_nonblock(conn_fd);
  ::connect(conn_fd, (::sockaddr*)(&this->_svr_addr), sizeof(this->_svr_addr));

  co_return Connection(conn_fd);
}

}  // namespace cgo::impl
#endif
