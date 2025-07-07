#include "core/timer.h"

namespace cgo::_impl::_time {

void TimedQueue::push(Task&& timer) {
  std::unique_lock guard(this->_mtx);
  if (this->_pq_timer.empty() || timer < this->_pq_timer.top()) {
    if (this->_signal) {
      this->_signal->emit();
    }
  }
  this->_pq_timer.push(std::forward<Task>(timer));
}

Task TimedQueue::pop() {
  std::unique_lock guard(this->_mtx);
  if (!this->_pq_timer.empty() && std::chrono::steady_clock::now() >= this->_pq_timer.top().ex) {
    auto timer = std::move(const_cast<Task&>(this->_pq_timer.top()));
    this->_pq_timer.pop();
    return timer;
  }
  if (!this->_pq_timer.empty()) {
    return Task(-1, nullptr, this->_pq_timer.top().ex);
  }
  return Task(-1, nullptr, Task::TimePoint::max());
}

}  // namespace cgo::_impl::_time

namespace cgo::_impl {

void TimedContext::create_timeout(std::function<void()>&& fn, std::chrono::duration<double, std::milli> timeout) {
  int id = this->_tid.fetch_add(1);
  int slot = id % this->_timed_queues.size();
  auto steady_timeout = std::chrono::duration_cast<std::chrono::steady_clock::duration>(timeout);
  auto ex_tp = std::chrono::steady_clock::now() + steady_timeout;
  this->_timed_queues[slot].push(_time::Task(id, std::forward<decltype(fn)>(fn), ex_tp));
}

size_t TimedContext::run_timeout(size_t pindex, size_t batch_size) {
  size_t cnt = 0;
  for (size_t slot = pindex % this->_timed_queues.size(); cnt < batch_size;) {
    auto task = this->_timed_queues[slot].pop();
    if (task) {
      task.fn();
      ++cnt;
    } else {
      break;
    }
  }
  if (cnt == batch_size) {
    return cnt;
  }
  for (size_t i = 0; i < this->_timed_queues.size(); ++i) {
    size_t slot = (pindex + i) % this->_timed_queues.size();
    if (auto task = this->_timed_queues[slot].pop(); task) {
      task.fn();
      ++cnt;
      if (cnt == batch_size) {
        break;
      }
    }
  }
  return cnt;
}

}  // namespace cgo::_impl

namespace cgo {

Channel<Nil> timeout(Context& ctx, std::chrono::duration<double, std::milli> timeout) {
  Channel<Nil> chan(1);
  _impl::TimedContext::at(ctx).create_timeout([chan]() mutable { chan.nowait() << Nil{}; }, timeout);
  return chan;
}

Coroutine<void> sleep(Context& ctx, std::chrono::duration<double, std::milli> timeout) {
  Semaphore sem(0);
  _impl::TimedContext::at(ctx).create_timeout([&sem]() { sem.release(); }, timeout);
  co_await sem.aquire();
}

}  // namespace cgo
