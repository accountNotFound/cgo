#pragma once

#include <atomic>
#include <optional>
#include <vector>

#include "core/channel.h"
#include "core/schedule.h"

namespace cgo::_impl {

class TimedContext {
 public:
  using TimePoint = std::chrono::time_point<std::chrono::steady_clock>;
  using Callback = std::function<void()>;

  static auto at(Context& ctx) -> TimedContext&;

  TimedContext(size_t n_partition) : _schedulers(n_partition) {}

  void create_timeout(std::function<void()>&& fn, std::chrono::duration<double, std::milli> timeout);

  void on_timeout(size_t pindex, BaseLazySignal& signal) { _schedulers[pindex]._signal = &signal; }

  size_t run_timeout(size_t pindex, size_t batch_size);

  auto next_schedule_time(size_t pindex) -> TimePoint;

 private:
  struct Task {
   public:
    size_t id = 0;
    Callback fn = nullptr;
    TimePoint ex = {};

    bool operator>(const Task& rhs) const { return ex > rhs.ex; }

    bool operator<(const Task& rhs) const { return ex < rhs.ex; }

    bool operator==(const Task& rhs) const { return ex == rhs.ex; }
  };

  class Scheduler {
    friend class TimedContext;

   public:
    void push(Task&& timer);

    auto pop() -> std::optional<Task>;

   private:
    Spinlock _mtx;
    std::priority_queue<Task, std::vector<Task>, std::greater<Task>> _pq_timer;
    _impl::BaseLazySignal* _signal = nullptr;
  };

  std::atomic<size_t> _tid = 0;
  std::vector<Scheduler> _schedulers;
};

}  // namespace cgo::_impl

namespace cgo {

Channel<Nil> timeout(Context& ctx, std::chrono::duration<double, std::milli> timeout);

Coroutine<void> sleep(Context& ctx, std::chrono::duration<double, std::milli> timeout);

}  // namespace cgo
