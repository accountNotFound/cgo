#include "./event.h"

#include "../core/condition.h"

// #define USE_DEBUG
#include "util/format.h"
// #undef USE_DEBUG

#if defined(linux) || defined(__linux) || defined(__linux__)
#include <sys/epoll.h>
#endif

namespace cgo::_impl {

#if defined(linux) || defined(__linux) || defined(__linux__)
Event Event::from_linux(size_t linux_event) {
  size_t cgo_event = 0;
  if (linux_event & ::EPOLLIN) cgo_event |= Event::IN;
  if (linux_event & ::EPOLLOUT) cgo_event |= Event::OUT;
  if (linux_event & ::EPOLLERR || linux_event & ::EPOLLHUP) cgo_event |= Event::ERR;
  if (linux_event & ::EPOLLONESHOT) cgo_event |= Event::ONESHOT;
  return cgo_event;
}

size_t Event::to_linux(Event cgo_event) {
  size_t linux_event = 0;
  if (cgo_event & Event::IN) linux_event |= ::EPOLLIN;
  if (cgo_event & Event::OUT) linux_event |= ::EPOLLOUT;
  if (cgo_event & Event::ERR) linux_event |= ::EPOLLERR;
  if (cgo_event & Event::ONESHOT) linux_event |= ::EPOLLONESHOT;
  return linux_event;
}
#endif

EventHandler::EventHandler() {
  EventHandler::current = this;
#if defined(linux) || defined(__linux) || defined(__linux__)
  this->_handler_fd = ::epoll_create(1024);
#endif
}

void EventHandler::add(Fd fd, Event on, std::function<void(Event)>&& callback) {
#if defined(linux) || defined(__linux) || defined(__linux__)
  std::unique_lock guard(this->_mutex);
  ::epoll_event ev;
  ev.events = Event::to_linux(on) | ::EPOLLET;
  ev.data.fd = fd;
  if (::epoll_ctl(this->_handler_fd, EPOLL_CTL_ADD, fd, &ev) == 0) {
    this->_callbacks[fd] = std::move(callback);
  }
#endif
}

void EventHandler::mod(Fd fd, Event on) {
#if defined(linux) || defined(__linux) || defined(__linux__)
  std::unique_lock guard(this->_mutex);
  ::epoll_event ev;
  ev.events = Event::to_linux(on) | ::EPOLLET;
  ev.data.fd = fd;
  ::epoll_ctl(this->_handler_fd, EPOLL_CTL_MOD, fd, &ev);
#endif
}

void EventHandler::del(Fd fd) {
#if defined(linux) || defined(__linux) || defined(__linux__)
  std::unique_lock guard(this->_mutex);
  ::epoll_ctl(this->_handler_fd, EPOLL_CTL_DEL, fd, nullptr);
  this->_callbacks.erase(fd);
#endif
}

size_t EventHandler::handle(size_t handle_batch, size_t timeout_ms) {
#if defined(linux) || defined(__linux) || defined(__linux__)
  std::vector<::epoll_event> ev_buffer(handle_batch);
  int active_num = ::epoll_wait(this->_handler_fd, ev_buffer.data(), ev_buffer.size(), timeout_ms);
  if (active_num <= 0) {
    return 0;
  }
  for (int i = 0; i < active_num; i++) {
    Fd fd = ev_buffer[i].data.fd;
    Event ev = Event::from_linux(ev_buffer[i].events);
    std::unique_lock guard(this->_mutex);
    if (this->_callbacks.contains(fd)) {
      DEBUG("notify fd{%d}, event=%lu\n", fd, size_t(ev));
      this->_callbacks[fd](ev);
    }
  }
  return active_num;
#endif
}

void EventHandler::loop(const std::function<bool()>& pred) {
  while (pred()) {
    this->handle();
  }
}

}  // namespace cgo::_impl
