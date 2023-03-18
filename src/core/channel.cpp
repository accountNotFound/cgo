#include "channel.h"

// #define USE_DEBUG
#include "util/log.h"

namespace cgo::impl {

class BlockSet : public CoroutineSet {
 public:
  BlockSet() : CoroutineSet() {}
  auto push_nolock(Coroutine&& coroutine) -> void { _queue.push(std::move(coroutine)); }
  auto pop_nolock() -> std::optional<Coroutine> {
    if (_queue.size() == 0) {
      return std::nullopt;
    }
    auto res = std::move(_queue.front());
    _queue.pop();
    return res;
  }

 private:
  SpinLock _real_mutex;
  std::queue<Coroutine> _queue;
};

ChanImpl::ChanImpl(size_t capacity) : _capacity(capacity) {
  this->_block_readers = std::make_unique<BlockSet>();
  this->_block_writers = std::make_unique<BlockSet>();
}

auto ChanImpl::send(std::any&& value) -> Async<void> {
  auto defer = [this]() { this->_mutex.unlock(); };
  while (true) {
    this->_mutex.lock();
    DEBUG("[TH-{%u}]: send: queue_size=%d", std::this_thread::get_id(), this->_queue.size());
    if (this->_queue.size() == this->_capacity) {
      co_await Context::current().wait(this->_block_writers.get(), defer);
    } else {
      this->_queue.push(std::move(value));
      Context::current().notify({this->_block_readers.get(), this->_block_writers.get()}, defer);
      co_return;
    }
  }
}

auto ChanImpl::recv() -> Async<std::any> {
  auto defer = [this]() { this->_mutex.unlock(); };
  while (true) {
    this->_mutex.lock();
    DEBUG("[TH-{%u}]: recv: queue_size=%d", std::this_thread::get_id(), this->_queue.size());
    if (this->_queue.size() == 0) {
      co_await Context::current().wait(this->_block_readers.get(), defer);
    } else {
      auto res = std::move(this->_queue.front());
      this->_queue.pop();
      Context::current().notify({this->_block_readers.get(), this->_block_writers.get()}, defer);
      co_return std::move(res);
    }
  }
}

auto ChanImpl::send_nowait(std::any&& value) -> bool {
  auto defer = [this]() { this->_mutex.unlock(); };
  this->_mutex.lock();
  if (this->_queue.size() == this->_capacity) {
    defer();
    return false;
  }
  this->_queue.push(std::move(value));
  Context::current().notify({this->_block_readers.get(), this->_block_writers.get()}, defer);
  return true;
}

auto ChanImpl::recv_nowait() -> std::any {
  auto defer = [this]() { this->_mutex.unlock(); };
  this->_mutex.lock();
  if (this->_queue.empty()) {
    defer();
    return std::any{};
  }
  auto res = std::move(this->_queue.front());
  this->_queue.pop();
  Context::current().notify({this->_block_readers.get(), this->_block_writers.get()}, defer);
  return res;
}

class Mutex::Impl {
 public:
  Impl() : _block_set(std::make_unique<BlockSet>()) {}
  auto lock() -> Async<void> {
    auto defer = [this]() { this->_mutex.unlock(); };
    while (true) {
      this->_mutex.lock();
      if (this->_lock_flag) {
        co_await Context::current().wait({this->_block_set.get()}, defer);
      } else {
        this->_lock_flag = true;
        defer();
        co_return;
      }
    }
  }
  auto unlock() -> void {
    std::unique_lock guard(this->_mutex);
    this->_lock_flag = false;
    Context::current().notify({this->_block_set.get()});
  }

 private:
  SpinLock _mutex;
  std::unique_ptr<BlockSet> _block_set;
  bool _lock_flag = false;
};

Mutex::Mutex() : _impl(std::make_shared<Impl>()) {}

auto Mutex::lock() -> Async<void> { co_await this->_impl->lock(); }

auto Mutex::unlock() -> void { this->_impl->unlock(); }

}  // namespace cgo::impl
