#include "./timer.h"

#include "./condition.h"

namespace cgo::_impl {

TimeHandler::TimeHandler() { TimeHandler::current = this; }

void TimeHandler::add(std::function<void()>&& func, int64_t timeout_ms) {
  std::unique_lock guard(this->_mutex);
  DelayTask task{std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms), std::move(func)};
  if (!this->_delay_pq.empty() && task < this->_delay_pq.top()) {
    this->_cond.notify_one();
  }
  this->_delay_pq.emplace(std::move(task));
}

void TimeHandler::handle(size_t batch_size, int64_t timeout_ms) {
  std::unique_lock guard(this->_mutex);
  auto now = std::chrono::steady_clock::now();
  if (this->_delay_pq.empty()) {
    this->_cond.wait_for(guard, std::chrono::milliseconds(timeout_ms));
  } else if (now + std::chrono::milliseconds(timeout_ms) < this->_delay_pq.top().expired_time) {
    this->_cond.wait_for(guard, std::chrono::milliseconds(timeout_ms));
  } else {
    this->_cond.wait_for(
        guard, std::chrono::duration_cast<std::chrono::milliseconds>(this->_delay_pq.top().expired_time - now));
  }
  for (int i = 0; i < batch_size && !this->_delay_pq.empty(); i++) {
    auto [expired_time, callback] = std::move(this->_delay_pq.top());
    this->_delay_pq.pop();
    this->_mutex.unlock();
    callback();
    this->_mutex.lock();
  }
}

void TimeHandler::loop(const std::function<bool()>& pred) {
  while (pred()) {
    this->handle();
  }
}

}  // namespace cgo::_impl

namespace cgo {

Coroutine<void> sleep(int64_t timeout_ms) {
  Channel<void*> chan;
  _impl::TimeHandler::current->add([chan]() mutable { chan.send_nowait(nullptr); }, timeout_ms);
  co_await chan.recv();
}

}  // namespace cgo