#include "event.h"

// #define USE_DEBUG
#include "util/log.h"

#if defined(linux) || defined(__linux) || defined(__linux__)
#include <sys/epoll.h>

#include <vector>

namespace cgo::impl {

auto Event::to_linux(Event cgo_event) -> size_t {
  size_t linux_event = 0;
  if (cgo_event & Event::IN) linux_event |= ::EPOLLIN;
  if (cgo_event & Event::OUT) linux_event |= ::EPOLLOUT;
  if (cgo_event & Event::ERR) linux_event |= ::EPOLLERR;
  if (cgo_event & Event::ONESHOT) linux_event |= ::EPOLLONESHOT;
  return linux_event;
}

auto Event::from_linux(size_t linux_event) -> Event {
  size_t cgo_event = 0;
  if (linux_event & ::EPOLLIN) cgo_event |= Event::IN;
  if (linux_event & ::EPOLLOUT) cgo_event |= Event::OUT;
  if (linux_event & ::EPOLLERR || linux_event & ::EPOLLHUP) cgo_event |= Event::ERR;
  if (linux_event & ::EPOLLONESHOT) cgo_event |= Event::ONESHOT;
  return cgo_event;
}

EventHandler::EventHandler(size_t fd_capacity) { this->_handler_fd = ::epoll_create(fd_capacity); }

EventHandler::~EventHandler() { ::close(this->_handler_fd); }

void EventHandler::add(Fd fd, Event on, Channel<Event>& chan) {
  std::unique_lock guard(this->_mtx);
  ::epoll_event ev;
  ev.events = Event::to_linux(on) | ::EPOLLET;
  ev.data.fd = fd;
  if (::epoll_ctl(this->_handler_fd, EPOLL_CTL_ADD, fd, &ev) == 0) {
    this->_fd_chans[fd] = chan;
  }
}

void EventHandler::mod(Fd fd, Event on) {
  std::unique_lock guard(this->_mtx);
  ::epoll_event ev;
  ev.events = Event::to_linux(on) | ::EPOLLET;
  ev.data.fd = fd;
  ::epoll_ctl(this->_handler_fd, EPOLL_CTL_MOD, fd, &ev);
}

void EventHandler::del(Fd fd) {
  std::unique_lock guard(this->_mtx);
  ::epoll_ctl(this->_handler_fd, EPOLL_CTL_DEL, fd, nullptr);
  this->_fd_chans.erase(fd);
}

auto EventHandler::handle(size_t handle_batch, size_t timeout_ms) -> size_t {
  std::vector<::epoll_event> ev_buffer(handle_batch);
  size_t active_num = ::epoll_wait(this->_handler_fd, ev_buffer.data(), ev_buffer.size(), timeout_ms);

  for (int i = 0; i < active_num; i++) {
    auto fd = ev_buffer[i].data.fd;
    auto cgo_ev = Event::from_linux(ev_buffer[i].events);
    if (cgo_ev == 0) {
      DEBUG("fd=%d get empty cgo_event", fd);
      continue;
    }
    std::unique_lock guard(_mtx);
    if (this->_fd_chans.contains(fd)) {
      DEBUG("fd=%d get cgo_event=%d", fd, cgo_ev);
      this->_fd_chans.at(fd).send_nowait(std::move(cgo_ev));
    }
  }
  return active_num;
}

}  // namespace cgo::impl
#endif