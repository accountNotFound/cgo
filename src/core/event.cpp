#include "core/event.h"

#if defined(linux) || defined(__linux) || defined(__linux__)

#include <sys/epoll.h>
#include <sys/eventfd.h>

#endif

namespace cgo::_impl::_event {

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

EventSignal::EventSignal(size_t p_index, size_t ev_batch_size) : _p_index(p_index), _ev_batch(ev_batch_size) {
  this->_fd = ::eventfd(0, ::EFD_NONBLOCK | ::EFD_CLOEXEC);
  get_dispatcher().add(this->_fd, Event::IN | Event::ONESHOT, [fd = this->_fd](Event ev) {
    int u;
    ::read(fd, &u, sizeof(u));
  });
}

void EventSignal::close() { ::close(this->_fd); }

void EventSignal::emit() {
  if (this->_wait_flag && !this->_signal_flag) {
    std::unique_lock guard(this->_mtx);
    if (this->_wait_flag && !this->_signal_flag) {
      get_dispatcher().mod(this->_fd, Event::IN | Event::ONESHOT);
      int u = 1;
      ::write(this->_fd, &u, sizeof(u));
      this->_signal_flag = true;
    }
  }
}

void EventSignal::wait(const std::chrono::duration<double, std::milli>& duration) {
  if (!this->_signal_flag) {
    std::unique_lock guard(this->_mtx);
    if (!this->_signal_flag) {
      this->_signal_flag = false;
      this->_wait_flag = true;
      guard.unlock();
      get_dispatcher().handle(this->_p_index, this->_ev_batch, duration);
      guard.lock();
      this->_wait_flag = false;
    }
  }
}

EventHandler::EventHandler() { this->_fd = ::epoll_create(1024); }

EventHandler::~EventHandler() { ::close(this->_fd); }

void EventHandler::add(Fd fd, Event on, std::function<void(Event)>&& callback) {
  std::unique_lock guard(this->_mtx);
  ::epoll_event ev;
  ev.events = Event::to_linux(on) | ::EPOLLET;
  ev.data.fd = fd;
  if (::epoll_ctl(this->_fd, EPOLL_CTL_ADD, fd, &ev) == 0) {
    this->_fd_calls[fd] = std::move(callback);
  }
}

void EventHandler::mod(Fd fd, Event on) {
  std::unique_lock guard(this->_mtx);
  ::epoll_event ev;
  ev.events = Event::to_linux(on) | ::EPOLLET;
  ev.data.fd = fd;
  ::epoll_ctl(this->_fd, EPOLL_CTL_MOD, fd, &ev);
}

void EventHandler::del(Fd fd) {
  std::unique_lock guard(this->_mtx);
  ::epoll_ctl(this->_fd, EPOLL_CTL_DEL, fd, nullptr);
  this->_fd_calls.erase(fd);
}

size_t EventHandler::handle(size_t handle_batch, size_t timeout_ms) {
  std::vector<::epoll_event> ev_buffer(handle_batch);
  int active_num = ::epoll_wait(this->_fd, ev_buffer.data(), ev_buffer.size(), timeout_ms);
  if (active_num <= 0) {
    return 0;
  }
  for (int i = 0; i < active_num; i++) {
    Fd fd = ev_buffer[i].data.fd;
    Event ev = Event::from_linux(ev_buffer[i].events);
    std::unique_lock guard(this->_mtx);
    if (this->_fd_calls.contains(fd)) {
      this->_fd_calls[fd](ev);
    }
  }
  return active_num;
}

#endif

}  // namespace cgo::_impl::_event
