#pragma once

#include <any>
#include <queue>

#include "context.h"

namespace cgo::impl {

class ChanImpl {
 public:
  ChanImpl(size_t capacity);
  auto send(std::any&& value) -> Async<void>;
  auto recv() -> Async<std::any>;
  auto send_nowait(std::any&& value) -> bool;
  auto recv_nowait() -> std::any;

 private:
  SpinLock _mutex;
  std::unique_ptr<CoroutineSet> _block_readers;
  std::unique_ptr<CoroutineSet> _block_writers;
  std::queue<std::any> _queue;
  const size_t _capacity;
};

template <NoVoid T>
class Channel {
 public:
  Channel() : _impl(std::make_shared<ChanImpl>(1)) {}
  Channel(size_t capacity) : _impl(std::make_shared<ChanImpl>(capacity)) {}
  auto send(T&& value) -> Async<void> { co_await this->_impl->send(std::make_any<T>(std::move(value))); }
  auto recv() -> Async<T> {
    if constexpr (!std::is_same_v<T, std::any>) {
      co_return std::any_cast<T>(co_await this->_impl->recv());
    } else {
      co_return co_await this->_impl->recv();
    }
  }
  auto send_nowait(T&& value) -> bool { return this->_impl->send_nowait(std::move(value)); }
  auto recv_nowait() -> std::optional<T> {
    auto res = this->_impl->recv_nowait();
    if (!res.has_value()) {
      return std::nullopt;
    }
    if constexpr (!std::is_same_v<T, std::any>) {
      return std::any_cast<T>(std::move(res));
    } else {
      return res;
    }
  }

 private:
  std::shared_ptr<ChanImpl> _impl;
};

class Mutex {
 public:
  Mutex();
  auto lock() -> Async<void>;
  void unlock();

 private:
  class Impl;
  std::shared_ptr<Impl> _impl;
};

}  // namespace cgo::impl
