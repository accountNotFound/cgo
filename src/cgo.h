#pragma once

#include "aio/event.h"
#include "aio/socket.h"
#include "core/condition.h"
#include "core/coroutine.h"
#include "core/executor.h"
#include "core/scheduler.h"
#include "util/defer.h"
#include "util/format.h"

namespace cgo {

using cgo::Channel;
using cgo::Coroutine;
using cgo::Mutex;
using cgo::Selector;
using cgo::Socket;
using cgo::SocketException;
using cgo::TimeoutException;
using Defer = cgo::util::Defer;

template <typename T>
extern void spawn(Coroutine<T>&& target);

extern Coroutine<void> yield();

extern Coroutine<void> sleep(int64_t timeout_ms);

template <typename T>
extern Coroutine<T> timeout(Coroutine<T>&& target, int64_t timeout_ms);

class Context {
 public:
  Context() : _executor(_scheduler, _time_handler, _event_handler) {}
  Context(const Context&) = delete;
  Context(Context&&) = delete;

  void start(size_t exec_num) { this->_executor.start(exec_num); }
  void stop() { this->_executor.stop(); }
  void loop(const std::function<bool()>& pred) {
    bool run_flag = true;
    auto run_pred = [&run_flag]() { return run_flag; };

    std::thread time_thread([this, &run_flag, &run_pred]() {
      _impl::Scheduler::current = &this->_scheduler;
      this->_time_handler.loop(run_pred);
    });
    std::thread event_thread([this, &run_flag, &run_pred]() {
      _impl::Scheduler::current = &this->_scheduler;
      this->_event_handler.loop(run_pred);
    });

    while (pred()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    run_flag = false;
    if (time_thread.joinable()) {
      time_thread.join();
    }
    if (event_thread.joinable()) {
      event_thread.join();
    }
  }

 private:
  _impl::Scheduler _scheduler;
  _impl::TimeHandler _time_handler;
  _impl::EventHandler _event_handler;
  _impl::Executor _executor;
};

}  // namespace cgo
