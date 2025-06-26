#include "core/timer.h"

namespace cgo::_impl::_time {

void DelayedQueue::push(Delayed&& timer) {
  std::unique_lock guard(this->_mtx);
  if (this->_pq_timer.empty() || timer < this->_pq_timer.top()) {
    if (this->_signal) {
      this->_signal->emit();
    }
  }
  this->_pq_timer.push(std::forward<Delayed>(timer));
}

Delayed DelayedQueue::pop() {
  std::unique_lock guard(this->_mtx);
  if (!this->_pq_timer.empty() && std::chrono::steady_clock::now() >= this->_pq_timer.top().ex) {
    auto timer = std::move(const_cast<Delayed&>(this->_pq_timer.top()));
    this->_pq_timer.pop();
    return timer;
  }
  if (!this->_pq_timer.empty()) {
    return Delayed(-1, nullptr, this->_pq_timer.top().ex);
  }
  return Delayed(-1, nullptr, Delayed::TimePoint::max());
}

void DelayedDispatcher::submit(std::function<void()>&& fn, const std::chrono::duration<double, std::milli>& timeout) {
  int id = this->_gid.fetch_add(1);
  int slot = id % this->_pq_timers.size();
  auto steady_timeout = std::chrono::duration_cast<std::chrono::steady_clock::duration>(timeout);
  auto ex_tp = std::chrono::steady_clock::now() + steady_timeout;
  this->_pq_timers[slot].push(Delayed(id, std::forward<decltype(fn)>(fn), ex_tp));
}

Delayed DelayedDispatcher::dispatch(size_t p_index) {
  for (int i = 0; i < this->_pq_timers.size(); ++i) {
    if (auto timer = this->_pq_timers[i].pop(); timer) {
      return timer;
    }
  }
  return this->_pq_timers[p_index].pop();
}

}  // namespace cgo::_impl::_time

namespace cgo {

Coroutine<void> sleep(const std::chrono::duration<double, std::milli>& timeout) {
  Semaphore sem(0);
  _impl::_time::get_dispatcher().submit([&sem]() { sem.release(); }, timeout);
  co_await sem.aquire();
}

}  // namespace cgo
