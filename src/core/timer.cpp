#include "./timer.h"

#include "./condition.h"

namespace cgo::_impl {

void Timer::add(std::function<void()>&& func, int64_t timeout_ms) {
  std::unique_lock guard(this->_mutex);
  this->_delay_pq.emplace(std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms), std::move(func));
}

void Timer::handle(int64_t timeout_ms) {
  std::unique_lock guard(this->_mutex);
  this->_cond.wait_for(guard, std::chrono::milliseconds(timeout_ms));
  while (!this->_delay_pq.empty() && std::chrono::steady_clock::now() >= this->_delay_pq.top().expired_time) {
    auto [_, callback] = std::move(this->_delay_pq.top());
    this->_delay_pq.pop();
    this->_mutex.unlock();
    callback();
    this->_mutex.lock();
  }
}

void Timer::loop(const std::function<bool()>& pred, int64_t timeout_interval_ms) {
  while (pred()) {
    this->handle(timeout_interval_ms);
  }
}

}  // namespace cgo::_impl

namespace cgo {

Coroutine<void> sleep(int64_t timeout_ms) {
  Channel<void*> chan;
  _impl::Timer::current->add([chan]() mutable { chan.send_nowait(nullptr); }, timeout_ms);
  co_await chan.recv();
}

}  // namespace cgo