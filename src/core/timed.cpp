#include "core/timed.h"

namespace cgo::_impl {

void TimedContext::create_timeout(std::function<void()>&& fn, std::chrono::duration<double, std::milli> timeout) {
  size_t id = this->_tid.fetch_add(1);
  size_t slot = id % this->_schedulers.size();
  auto steady_timeout = std::chrono::duration_cast<std::chrono::steady_clock::duration>(timeout);
  auto ex_tp = std::chrono::steady_clock::now() + steady_timeout;
  this->_schedulers[slot].push(Task{id, std::forward<decltype(fn)>(fn), ex_tp});
}

size_t TimedContext::run_timeout(size_t pindex, size_t batch_size) {
  size_t cnt = 0;
  for (size_t slot = pindex % this->_schedulers.size(); cnt < batch_size;) {
    auto task = this->_schedulers[slot].pop();
    if (task) {
      task->fn();
      ++cnt;
    } else {
      break;
    }
  }
  if (cnt == batch_size) {
    return cnt;
  }
  for (size_t i = 0; i < this->_schedulers.size(); ++i) {
    size_t slot = (pindex + i) % this->_schedulers.size();
    if (auto task = this->_schedulers[slot].pop(); task) {
      task->fn();
      ++cnt;
      if (cnt == batch_size) {
        break;
      }
    }
  }
  return cnt;
}

auto TimedContext::next_schedule_time(size_t pindex) -> TimedContext::TimePoint {
  size_t slot = pindex % _schedulers.size();
  auto& scheduler = _schedulers[slot];
  {
    std::unique_lock guard(scheduler._mtx);
    if (scheduler._pq_timer.empty()) {
      return TimePoint::max();
    }
    return scheduler._pq_timer.top().ex;
  }
}

void TimedContext::Scheduler::push(Task&& timer) {
  std::unique_lock guard(_mtx);
  if (_pq_timer.empty() || timer < _pq_timer.top()) {
    if (_signal) {
      _signal->emit();
    }
  }
  _pq_timer.push(std::forward<Task>(timer));
}

auto TimedContext::Scheduler::pop() -> std::optional<Task> {
  std::unique_lock guard(_mtx);
  if (!_pq_timer.empty() && std::chrono::steady_clock::now() >= _pq_timer.top().ex) {
    auto timer = std::move(const_cast<Task&>(_pq_timer.top()));
    _pq_timer.pop();
    return timer;
  }
  return std::nullopt;
}

}  // namespace cgo::_impl

namespace cgo {

Channel<Nil> timeout(Context& ctx, std::chrono::duration<double, std::milli> timeout) {
  Channel<Nil> chan(1);
  _impl::TimedContext::at(ctx).create_timeout([chan]() mutable { chan.nowait() << Nil{}; }, timeout);
  return chan;
}

Coroutine<void> sleep(Context& ctx, std::chrono::duration<double, std::milli> timeout) {
  Semaphore signal(0);
  _impl::TimedContext::at(ctx).create_timeout([&signal]() { signal.release(); }, timeout);
  co_await signal.aquire();
}

}  // namespace cgo
