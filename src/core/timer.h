#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <queue>

#include "./coroutine.h"
#include "./scheduler.h"
#include "util/defer.h"
#include "util/slock.h"

namespace cgo::_impl {

class TimeHandler {
 public:
  static thread_local TimeHandler* current;

 public:
  TimeHandler() { TimeHandler::current = this; }
  void add(std::function<void()>&& func, int64_t timeout_ms);
  void handle(int64_t timeout_ms = 50);
  void loop(const std::function<bool()>& pred, int64_t timeout_interval_ms = 50);

 private:
  struct DelayTask {
    std::chrono::time_point<std::chrono::steady_clock> expired_time;
    std::function<void()> callback;

    bool operator>(const DelayTask& rhs) const { return this->expired_time > rhs.expired_time; }
  };

  std::mutex _mutex;
  std::condition_variable _cond;
  // TODO: optimize with event handler

  std::priority_queue<DelayTask, std::vector<DelayTask>, std::greater<DelayTask>> _delay_pq;
};

}  // namespace cgo::_impl

namespace cgo {

Coroutine<void> sleep(int64_t timeout_ms);

// throw cgo::TimeoutException if timeout. Only works when coroutine finally await on cgo::_impl::Condition. All builtin
// functions and classes support this operation
template <typename T>
Coroutine<T> timeout(Coroutine<T>&& target, int64_t timeout_ms) {
  _impl::Task* current = _impl::Task::current;
  current->await_timeout_ms = timeout_ms;
  util::Defer defer([current]() { current->await_timeout_ms = -1; });

  if constexpr (std::is_same_v<T, void>) {
    co_await target;
    // TODO: cancel corresponding timer
  } else {
    T res = co_await target;
    // TODO: cancel corresponding timer
    co_return std::move(res);
  }
}

}  // namespace cgo
