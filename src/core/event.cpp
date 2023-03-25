#include "event.h"

namespace cgo::impl {

Event::Type operator|(Event::Type a, Event::Type b) {
  return static_cast<Event::Type>(static_cast<size_t>(a) | static_cast<size_t>(b));
}

}  // namespace cgo::impl

#if defined(linux) || defined(__linux) || defined(__linux__)
#include <sys/epoll.h>

#include <vector>

namespace cgo::impl {

auto Event::to_linux() const -> size_t {
  size_t res = 0;
  if (this->types() | Event::Type::IN) res |= EPOLLIN;
  if (this->types() | Event::Type::OUT) res |= EPOLLOUT;
  if (this->types() | Event::Type::ERR) res |= EPOLLERR;
  if (this->types() | Event::Type::ONESHOT) res |= EPOLLONESHOT;
  return res;
}

auto Event::from_linux(size_t linux_event) const -> Event {
  Event res = *this;
  size_t cgo_event = 0;
  if (linux_event & EPOLLIN) cgo_event |= Event::Type::IN;
  if (linux_event & EPOLLOUT) cgo_event |= Event::Type::OUT;
  if (linux_event & EPOLLERR) cgo_event |= Event::Type::ERR;
  res._types = static_cast<Event::Type>(cgo_event);
  return res;
}

EventHandler::EventHandler(size_t fd_capacity) { this->_handler_fd = ::epoll_create(fd_capacity); }

EventHandler::~EventHandler() { ::close(this->_handler_fd); }

auto EventHandler::add(Fd fd, Event& on) -> void {
  ::epoll_event ev;
  ev.events = on.to_linux();
  ev.data.ptr = &on;
  ::epoll_ctl(this->_handler_fd, EPOLL_CTL_ADD, fd, &ev);
}
auto EventHandler::mod(Fd fd, Event& on) -> void {
  ::epoll_event ev;
  ev.events = on.to_linux();
  ev.data.ptr = &on;
  ::epoll_ctl(this->_handler_fd, EPOLL_CTL_MOD, fd, &ev);
}
auto EventHandler::del(Fd fd) -> void {
  ::epoll_ctl(this->_handler_fd, EPOLL_CTL_DEL, fd, nullptr);
}
auto EventHandler::handle(size_t handle_batch, size_t timeout_ms) -> size_t {
  std::vector<::epoll_event> ev_buffer(handle_batch);
  size_t active_num = ::epoll_wait(this->_handler_fd, ev_buffer.data(), ev_buffer.size(), timeout_ms);
  for (int i = 0; i < active_num; i++) {
    auto ev = static_cast<Event*>(ev_buffer[i].data.ptr);
    auto ok = ev->chan().send_nowait(ev->from_linux(ev_buffer[i].events).types());
    if (!ok) {
      // TOOD: optimization
      // current implementation works in LT mode default and data would not lost, simply send_nowait in next handle()
    }
  }
  return active_num;
}

}  // namespace cgo::impl
#endif