#pragma once

#include "core/condition.h"
#include "core/coroutine.h"
#include "core/executor.h"
#include "core/scheduler.h"

namespace cgo {

using cgo::Channel;
using cgo::Coroutine;
using cgo::Mutex;

class Context {
 public:
  Context() : _scheduler(), _executor(&_scheduler) {}
  Context(const Context&) = delete;
  Context(Context&&) = delete;

  void start(size_t exec_num) { this->_executor.start(exec_num); }
  void stop() { this->_executor.stop(); }

 private:
  _impl::Scheduler _scheduler;
  _impl::Executor _executor;
};

template <typename T>
void spawn(Coroutine<T>&& target) {
  _impl::Scheduler::current->allocate(_impl::Task(std::move(target)));
}

extern std::suspend_always yield();

class Defer {
 public:
  Defer(std::function<void()>&& callback) : _callback(callback) {}
  ~Defer() { this->_callback(); }

 private:
  std::function<void()> _callback;
};

}  // namespace cgo
