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

size_t to_linux_event(Event& event) {
  size_t res = 0;
  if (event.types() | Event::Type::IN) res |= EPOLLIN;
  if (event.types() | Event::Type::OUT) res |= EPOLLOUT;
  if (event.types() | Event::Type::ERR) res |= EPOLLERR;
  if (event.types() | Event::Type::ONESHOT) res |= EPOLLONESHOT;
  return res;
}

EventHandler::EventHandler(size_t fd_capacity) { this->_handler_fd = ::epoll_create(fd_capacity); }

EventHandler::~EventHandler() { ::close(this->_handler_fd); }

auto EventHandler::add(Fd fd, Event& on) -> void {
  ::epoll_event ev;
  ev.events = to_linux_event(on);
  ev.data.ptr = &on;
  ::epoll_ctl(this->_handler_fd, EPOLL_CTL_ADD, fd, &ev);
}
auto EventHandler::mod(Fd fd, Event& on) -> void {
  ::epoll_event ev;
  ev.events = to_linux_event(on);
  ev.data.ptr = &on;
  ::epoll_ctl(this->_handler_fd, EPOLL_CTL_MOD, fd, &ev);
}
auto EventHandler::del(Fd fd, Event& on) -> void {
  ::epoll_ctl(this->_handler_fd, EPOLL_CTL_DEL, fd, nullptr);
  on.chan().send_nowait(false);
}
auto EventHandler::handle(size_t handle_batch, size_t timeout_ms) -> size_t {
  std::vector<::epoll_event> ev_buffer(handle_batch);
  size_t active_num = ::epoll_wait(this->_handler_fd, ev_buffer.data(), ev_buffer.size(), timeout_ms);
  for (int i = 0; i < active_num; i++) {
    auto ev = static_cast<Event*>(ev_buffer[i].data.ptr);
    auto ok = ev->chan().send_nowait(true);
  }
  return active_num;
}

}  // namespace cgo::impl
#endif