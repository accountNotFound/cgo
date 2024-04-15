#pragma once

#include "core/condition.h"
#include "core/coroutine.h"
#include "core/error.h"
#include "core/executor.h"
#include "core/scheduler.h"
#include "util/defer.h"

namespace cgo {

using cgo::Channel;
using cgo::Coroutine;
using cgo::Mutex;
using cgo::Selector;
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
  Context() : _executor(_scheduler, _time_handler) {}
  Context(const Context&) = delete;
  Context(Context&&) = delete;

  void start(size_t exec_num) { this->_executor.start(exec_num); }
  void stop() { this->_executor.stop(); }
  void loop(const std::function<bool()>& pred) { this->_time_handler.loop(pred); }

 private:
  _impl::Scheduler _scheduler;
  _impl::Executor _executor;
  _impl::TimeHandler _time_handler;
};

}  // namespace cgo
