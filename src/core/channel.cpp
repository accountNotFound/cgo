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

ChanTrait::ChanTrait(size_t capacity) : _capacity(capacity) {
  this->_mutex = std::make_shared<SpinLock>();
  this->_block_readers = std::make_shared<BlockSet>();
  this->_block_writers = std::make_shared<BlockSet>();
}

auto ChanTrait::_send(std::any&& value) -> Async<void> {
  auto defer = [this]() { this->_mutex->unlock(); };
  while (true) {
    this->_mutex->lock();
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

auto ChanTrait::_recv() -> Async<std::any> {
  auto defer = [this]() { this->_mutex->unlock(); };
  while (true) {
    this->_mutex->lock();
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

}  // namespace cgo::impl
