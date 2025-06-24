#pragma once

#include <atomic>
#include <vector>

#include "core/schedule.h"

namespace cgo::_impl::_time {

struct Timer {
  using TimePoint = std::chrono::time_point<std::chrono::steady_clock>;
  using Callback = std::function<void()>;

  Callback fn;
  TimePoint ex;

  Timer() : _id(-1), fn(nullptr), ex() {}

  Timer(int id, Callback&& fn, const TimePoint& ex) : _id(id), fn(std::move(fn)), ex(ex) {}

  operator bool() const { return this->_id >= 0; }

  bool operator>(const Timer& rhs) const { return this->ex > rhs.ex; }

  bool operator<(const Timer& rhs) const { return this->ex < rhs.ex; }

  bool operator==(const Timer& rhs) const { return this->ex == rhs.ex; }

 private:
  int _id;
};

class TimerQueue {
 public:
  void push(Timer&& timer);

  Timer pop();

  void regist(Signal& signal) { this->_signal = &signal; }

 private:
 private:
  Spinlock _mtx;
  std::priority_queue<Timer, std::vector<Timer>, std::greater<Timer>> _pq_timer;
  Signal* _signal;
};

class TimerDispatcher {
 public:
  TimerDispatcher(size_t n_partition) : _gid(0), _q_timers(n_partition) {}

  void submit(std::function<void()>&& fn, const std::chrono::duration<double, std::milli>& timeout);

  Timer dispatch(size_t p_index);

  void regist(size_t p_index, Signal& signal) { this->_q_timers[p_index].regist(signal); }

 private:
  std::atomic<int> _gid;
  std::vector<TimerQueue> _q_timers;
};

inline std::unique_ptr<TimerDispatcher> g_dispatcher = nullptr;

inline TimerDispatcher& get_dispatcher() { return *g_dispatcher; }

}  // namespace cgo::_impl::_time

namespace cgo {

Coroutine<void> sleep(const std::chrono::duration<double, std::milli>& timeout);

}  // namespace cgo
