#pragma once

#include <atomic>
#include <vector>

#include "core/schedule.h"

namespace cgo::_impl::_time {

struct Delayed {
  using TimePoint = std::chrono::time_point<std::chrono::steady_clock>;
  using Callback = std::function<void()>;

  Callback fn;
  TimePoint ex;

  Delayed() : _id(-1), fn(nullptr), ex() {}

  Delayed(int id, Callback&& fn, const TimePoint& ex) : _id(id), fn(std::forward<decltype(fn)>(fn)), ex(ex) {}

  operator bool() const { return this->_id >= 0; }

  bool operator>(const Delayed& rhs) const { return this->ex > rhs.ex; }

  bool operator<(const Delayed& rhs) const { return this->ex < rhs.ex; }

  bool operator==(const Delayed& rhs) const { return this->ex == rhs.ex; }

 private:
  int _id;
};

class DelayedQueue {
 public:
  void push(Delayed&& timer);

  Delayed pop();

  void regist(_impl::SignalBase& signal) { this->_signal = &signal; }

 private:
 private:
  Spinlock _mtx;
  std::priority_queue<Delayed, std::vector<Delayed>, std::greater<Delayed>> _pq_timer;
  _impl::SignalBase* _signal;
};

class DelayedDispatcher {
 public:
  DelayedDispatcher(size_t n_partition) : _tid(0), _pq_timers(n_partition) {}

  void submit(std::function<void()>&& fn, const std::chrono::duration<double, std::milli>& timeout);

  Delayed dispatch(size_t p_index);

  void regist(size_t p_index, _impl::SignalBase& signal) { this->_pq_timers[p_index].regist(signal); }

 private:
  std::atomic<int> _tid;
  std::vector<DelayedQueue> _pq_timers;
};

inline std::unique_ptr<DelayedDispatcher> g_dispatcher = nullptr;

inline DelayedDispatcher& get_dispatcher() { return *g_dispatcher; }

}  // namespace cgo::_impl::_time

namespace cgo {

Coroutine<void> sleep(std::chrono::duration<double, std::milli> timeout);

}  // namespace cgo
