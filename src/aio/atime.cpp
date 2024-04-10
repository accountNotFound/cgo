#include "./atime.h"

#if defined(linux) || defined(__linux) || defined(__linux__)
#include <sys/timerfd.h>
#endif

namespace cgo {

Timer::Timer(unsigned long long millisec) : _chan(1) {
#if defined(linux) || defined(__linux) || defined(__linux__)
  this->_fd = ::timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
  ::itimerspec ts;
  ts.it_interval = timespec{0, 0};
  ts.it_value = timespec{(long)millisec / 1000, ((long)millisec % 1000) * 1000000};
  timerfd_settime(_fd, 0, &ts, nullptr);
  _impl::EventHandler::current->add(this->_fd, _impl::Event::IN | _impl::Event::ONESHOT,
                                         [this](_impl::Event ev) { this->_chan.send_nowait(nullptr); });
#endif
}

Timer::~Timer() {
  _impl::EventHandler::current->del(this->_fd);
  ::close(this->_fd);
}

Coroutine<void> sleep(unsigned long long milli_sec) {
  Timer timer(milli_sec);
  co_await timer.chan().recv();
}

}  // namespace cgo
