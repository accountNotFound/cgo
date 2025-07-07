#pragma once

#include <atomic>
#include <vector>

#include "core/channel.h"
#include "core/schedule.h"

namespace cgo::_impl::_time {

struct Task {
  using TimePoint = std::chrono::time_point<std::chrono::steady_clock>;
  using Callback = std::function<void()>;

  Callback fn;
  TimePoint ex;

  Task() : _id(-1), fn(nullptr), ex() {}

  Task(int id, Callback&& fn, const TimePoint& ex) : _id(id), fn(std::forward<decltype(fn)>(fn)), ex(ex) {}

  operator bool() const { return this->_id >= 0; }

  bool operator>(const Task& rhs) const { return this->ex > rhs.ex; }

  bool operator<(const Task& rhs) const { return this->ex < rhs.ex; }

  bool operator==(const Task& rhs) const { return this->ex == rhs.ex; }

 private:
  int _id;
};

class TimedQueue {
 public:
  void push(Task&& timer);

  Task pop();

  void on_timeout(_impl::BaseLazySignal& signal) { this->_signal = &signal; }

 private:
  Spinlock _mtx;
  std::priority_queue<Task, std::vector<Task>, std::greater<Task>> _pq_timer;
  _impl::BaseLazySignal* _signal;
};

}  // namespace cgo::_impl::_time

namespace cgo::_impl {

class TimedContext {
 public:
  static auto at(Context& ctx) -> TimedContext&;

  TimedContext(size_t n_partition) : _timed_queues(n_partition) {}

  void create_timeout(std::function<void()>&& fn, std::chrono::duration<double, std::milli> timeout);

  void on_timeout(size_t pindex, BaseLazySignal& signal) { _timed_queues[pindex].on_timeout(signal); }

  size_t run_timeout(size_t pindex, size_t batch_size);

 private:
  std::atomic<size_t> _tid = 0;

  std::vector<_impl::_time::TimedQueue> _timed_queues;
};

}  // namespace cgo::_impl

namespace cgo {

Channel<Nil> timeout(Context& ctx, std::chrono::duration<double, std::milli> timeout);

Coroutine<void> sleep(Context& ctx, std::chrono::duration<double, std::milli> timeout);

}  // namespace cgo
