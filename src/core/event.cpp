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
  if (linux_event & ::EPOLLERR) cgo_event |= Event::ERR;
  if (linux_event & ::EPOLLONESHOT) cgo_event |= Event::ONESHOT;
  return cgo_event;
}

EventHandler::EventHandler(size_t fd_capacity) { this->_handler_fd = ::epoll_create(fd_capacity); }

EventHandler::~EventHandler() { ::close(this->_handler_fd); }

auto EventHandler::add(Fd fd, Event on) -> Channel<Event> {
  ::epoll_event ev;
  std::unique_lock guard(this->_mtx);
  this->_fd_chan[fd] = Channel<Event>();
  ev.events = Event::to_linux(on) | ::EPOLLET;
  ev.data.fd = fd;
  ::epoll_ctl(this->_handler_fd, EPOLL_CTL_ADD, fd, &ev);
  return this->_fd_chan.at(fd);
}

void EventHandler::mod(Fd fd, Event on) {
  ::epoll_event ev;
  ev.events = Event::to_linux(on);
  ev.data.fd = fd;
  ::epoll_ctl(this->_handler_fd, EPOLL_CTL_MOD, fd, &ev);
}

auto EventHandler::del(Fd fd) -> void {
  std::unique_lock guard(this->_mtx);
  this->_fd_chan.erase(fd);
  ::epoll_ctl(this->_handler_fd, EPOLL_CTL_DEL, fd, nullptr);
}

auto EventHandler::handle(size_t handle_batch, size_t timeout_ms) -> size_t {
  std::vector<::epoll_event> ev_buffer(handle_batch);
  size_t active_num = ::epoll_wait(this->_handler_fd, ev_buffer.data(), ev_buffer.size(), timeout_ms);

  std::unique_lock guard(this->_mtx);
  for (int i = 0; i < active_num; i++) {
    if (this->_fd_chan.contains(ev_buffer[i].data.fd)) {
      auto& chan = this->_fd_chan.at(ev_buffer[i].data.fd);
      chan.send_nowait(Event::from_linux(ev_buffer[i].events));
    }
  }
  return active_num;
}

}  // namespace cgo::impl
#endif