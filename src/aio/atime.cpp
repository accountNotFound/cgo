#include "atime.h"

#if defined(linux) || defined(__linux) || defined(__linux__)
#include <sys/timerfd.h>

namespace cgo::impl {

Timer::Timer(unsigned long long millisec) {
  this->_fd = ::timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
  ::itimerspec ts;
  ts.it_interval = timespec{0, 0};
  ts.it_value = timespec{(long)millisec / 1000, ((long)millisec % 1000) * 1000000};
  timerfd_settime(_fd, 0, &ts, nullptr);
  Context::current().handler().add(this->_fd, Event::IN | Event::ONESHOT,
                                   [this](Event event) { this->_chan.send_nowait(std::move(event)); });
}

Timer::~Timer() {
  Context::current().handler().del(this->_fd);
  ::close(this->_fd);
}

auto Timer::wait() -> Async<void> { co_await this->_chan.recv(); }

auto sleep(unsigned long long millisec) -> Async<void> { co_await Timer(millisec).wait(); }

}  // namespace cgo::impl
#endif
