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
                                    [fd = this->_fd, chan = this->_chan](_impl::Event ev) mutable {
                                      chan.send_nowait(nullptr);
                                      ::close(fd);
                                    });
#endif
}

Coroutine<void> sleep(unsigned long long milli_sec) {
  Timer timer(milli_sec);
  co_await timer.chan().recv();
}

}  // namespace cgo
