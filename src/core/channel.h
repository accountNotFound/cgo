#pragma once

#include <any>
#include <queue>

#include "context.h"

namespace cgo::impl {

class ChanTrait {
 protected:
  ChanTrait(size_t capacity);
  auto _send(std::any&& value) -> Async<void>;
  auto _recv() -> Async<std::any>;
  auto _send_nowait(std::any&& value) -> bool;
  auto _recv_nowait() -> std::any;

 private:
  std::shared_ptr<SpinLock> _mutex;
  std::shared_ptr<CoroutineSet> _block_readers;
  std::shared_ptr<CoroutineSet> _block_writers;
  std::shared_ptr<std::queue<std::any>> _queue;
  const size_t _capacity;
};

template <NoVoid T>
class Channel : private ChanTrait {
 public:
  Channel(size_t capacity) : ChanTrait(capacity) {}
  auto send(T&& value) -> Async<void> {
    co_await this->_send(std::make_any<T>(std::move(value)));
    co_return;
  }
  auto recv() -> Async<T> {
    if constexpr (!std::is_same_v<T, std::any>) {
      co_return std::any_cast<T>(co_await this->_recv());
    } else {
      co_return co_await this->_recv();
    }
  }
  auto send_nowait(T&& value) -> bool { return this->_send_nowait(std::move(value)); }
  auto recv_nowait() -> std::optional<T> {
    auto res = this->_recv_nowait();
    if (!res.has_value()) {
      return std::nullopt;
    }
    if constexpr (!std::is_same_v<T, std::any>) {
      return std::any_cast<T>(std::move(res));
    } else {
      return res;
    }
  }
};

}  // namespace cgo::impl