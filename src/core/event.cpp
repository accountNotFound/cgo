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

EventHandler::EventHandler() { this->_fd = ::epoll_create(1024); }

EventHandler::~EventHandler() { ::close(this->_fd); }

void EventHandler::add(int fd, Event on, std::function<void(Event)>&& fn) {
  std::unique_lock guard(this->_mtx);
  ::epoll_event ev;
  ev.events = Event::to_linux(on) | ::EPOLLET;
  int cid = ev.data.u64 = this->_tid.fetch_add(1);

  this->_m_calls[cid] = {fd, on, std::forward<decltype(fn)>(fn)};
  this->_fd_cids[fd] = cid;

  if (::epoll_ctl(this->_fd, EPOLL_CTL_ADD, fd, &ev) != 0) {
    throw std::runtime_error("epoll_ctl add failed");
  }
}

void EventHandler::mod(int fd, Event on, std::function<void(Event)>&& fn) {
  std::unique_lock guard(this->_mtx);
  ::epoll_event ev;
  ev.events = Event::to_linux(on) | ::EPOLLET;
  int cid = ev.data.u64 = this->_tid.fetch_add(1);

  this->_m_calls[cid] = {fd, on, std::forward<decltype(fn)>(fn)};
  this->_m_calls.erase(this->_fd_cids[fd]);
  this->_fd_cids[fd] = cid;

  if (::epoll_ctl(this->_fd, EPOLL_CTL_MOD, fd, &ev) != 0) {
    throw std::runtime_error("epoll_ctl mod failed");
  }
}

void EventHandler::del(int fd) {
  std::unique_lock guard(this->_mtx);
  ::epoll_ctl(this->_fd, EPOLL_CTL_DEL, fd, nullptr);

  this->_m_calls.erase(this->_fd_cids[fd]);
  this->_fd_cids.erase(fd);
}

size_t EventHandler::handle(size_t handle_batch, size_t timeout_ms) {
  std::vector<::epoll_event> ev_buffer(handle_batch);
  int active_num = ::epoll_wait(this->_fd, ev_buffer.data(), ev_buffer.size(), timeout_ms);
  if (active_num <= 0) {
    return 0;
  }
  for (int i = 0; i < active_num; ++i) {
    int cid = ev_buffer[i].data.u64;
    Event ev = Event::from_linux(ev_buffer[i].events);
    std::unique_lock guard(this->_mtx);
    if (this->_m_calls.contains(cid)) {
      auto& callback = this->_m_calls[cid];
      if (callback.on & ev) {
        callback.fn(ev);
      }
    }
  }
  return active_num;
}

#endif

}  // namespace cgo::_impl::_event

namespace cgo::_impl {

EventSignal::EventSignal() {
  this->_fd = ::eventfd(0, ::EFD_NONBLOCK | ::EFD_CLOEXEC);
  _event::get_dispatcher().add(this->_fd, _event::Event::IN | _event::Event::ONESHOT,
                               [this](_event::Event ev) { this->_callback(ev); });
}

void EventSignal::close() {
  _event::get_dispatcher().del(this->_fd);
  ::close(this->_fd);
}

void EventSignal::_emit() { ::write(this->_fd, &this->_event_flag, sizeof(this->_event_flag)); }

void EventSignal::_wait(std::unique_lock<Spinlock>& guard, std::chrono::duration<double, std::milli> duration) {
  this->_cond.wait_for(guard, duration);
  _event::get_dispatcher().mod(this->_fd, _event::Event::IN | _event::Event::ONESHOT,
                               [this](_event::Event ev) { this->_callback(ev); });
}

void EventSignal::_callback(_event::Event ev) {
  std::unique_lock guard(this->_mtx);
  ::read(this->_fd, &this->_event_flag, sizeof(this->_event_flag));
  this->_cond.notify_one();
}

}  // namespace cgo::_impl
