#include "core/event.h"

#if defined(linux) || defined(__linux) || defined(__linux__)

#include <sys/epoll.h>
#include <sys/eventfd.h>

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

EventContext::Dispatcher::Dispatcher() { _fd = ::epoll_create(1024); }

EventContext::Dispatcher::~Dispatcher() { ::close(_fd); }

void EventContext::Dispatcher::add(int fd, Event on, std::function<void(Event)>&& fn) {
  std::unique_lock guard(_mtx);
  ::epoll_event ev;
  ev.events = Event::to_linux(on) | ::EPOLLET;
  int hid = ev.data.u64 = _gid.fetch_add(1);

  _hid_calls[hid] = {fd, on, std::forward<decltype(fn)>(fn)};
  _fd_hids[fd] = hid;

  if (::epoll_ctl(_fd, EPOLL_CTL_ADD, fd, &ev) != 0) {
    throw std::runtime_error("epoll_ctl add failed");
  }
}

void EventContext::Dispatcher::mod(int fd, Event on, std::function<void(Event)>&& fn) {
  std::unique_lock guard(_mtx);
  ::epoll_event ev;
  ev.events = Event::to_linux(on) | ::EPOLLET;
  int hid = ev.data.u64 = _gid.fetch_add(1);

  _hid_calls[hid] = {fd, on, std::forward<decltype(fn)>(fn)};
  _hid_calls.erase(_fd_hids[fd]);
  _fd_hids[fd] = hid;

  if (::epoll_ctl(_fd, EPOLL_CTL_MOD, fd, &ev) != 0) {
    throw std::runtime_error("epoll_ctl mod failed");
  }
}

void EventContext::Dispatcher::del(int fd) {
  std::unique_lock guard(_mtx);
  ::epoll_ctl(_fd, EPOLL_CTL_DEL, fd, nullptr);

  _hid_calls.erase(_fd_hids[fd]);
  _fd_hids.erase(fd);
}

size_t EventContext::Dispatcher::handle(size_t handle_batch, size_t timeout_ms) {
  std::vector<::epoll_event> ev_buffer(handle_batch);
  int active_num = ::epoll_wait(_fd, ev_buffer.data(), ev_buffer.size(), timeout_ms);
  if (active_num <= 0) {
    return 0;
  }
  for (int i = 0; i < active_num; ++i) {
    int hid = ev_buffer[i].data.u64;
    Event ev = Event::from_linux(ev_buffer[i].events);
    std::unique_lock guard(this->_mtx);
    if (_hid_calls.contains(hid)) {
      auto& callback = _hid_calls[hid];
      if (callback.on & ev) {
        callback.fn(ev);
      }
    }
  }
  return active_num;
}

#endif

EventLazySignal::EventLazySignal(Context& ctx, size_t pindex, bool nowait)
    : _ctx(&ctx), _pindex(pindex), _nowait(nowait) {
  _fd = ::eventfd(0, ::EFD_NONBLOCK | ::EFD_CLOEXEC);
  EventContext::at(ctx).handler(_pindex).add(_fd, Event::IN | Event::ONESHOT, [this](Event ev) { _callback(ev); });
}

void EventLazySignal::close() {
  EventContext::at(*_ctx).handler(_pindex).del(_fd);
  ::close(_fd);
}

void EventLazySignal::_emit() { ::write(_fd, &_sig_data, sizeof(_sig_data)); }

void EventLazySignal::_wait(std::unique_lock<Spinlock>& guard, std::chrono::duration<double, std::milli> duration) {
  if (!_nowait) {
    _cond.wait_for(guard, duration);
    EventContext::at(*_ctx).handler(_pindex).mod(_fd, Event::IN | Event::ONESHOT, [this](Event ev) { _callback(ev); });
  }
}

void EventLazySignal::_callback(Event ev) {
  std::unique_lock guard(this->_mtx);
  ::read(_fd, &_sig_data, sizeof(_sig_data));
  if (!_nowait) {
    _cond.notify_one();
  }
}

}  // namespace cgo::_impl
