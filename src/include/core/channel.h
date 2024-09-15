#pragma once

#include <functional>

#include "core/schedule.h"

namespace cgo::_impl::_chan {

template <typename T>
class ChannelImpl {
 public:
  using Delegator = std::function<bool(T*)>;

  ChannelImpl(size_t capacity) : _capacity(capacity) {}

  bool send(T* src, Delegator sender) {
    std::unique_lock guard(this->_mtx);
    if (this->_buffer.size() < this->_capacity) {
      this->_buffer.push(std::move(*src));
      if (this->_delegate(this->_recievers, &this->_buffer.front())) {
        this->_buffer.pop();
      }
      return true;
    }
    if (this->_capacity > 0) {
      if (this->_delegate(this->_recievers, &this->_buffer.front())) {
        this->_buffer.pop();
        this->_buffer.push(std::move(*src));
        return true;
      }
    } else {
      if (this->_delegate(this->_recievers, src)) {
        return true;
      }
    }
    this->_senders.push(std::move(sender));
    return false;
  }

  bool recv(T* dst, Delegator reciever) {
    std::unique_lock guard(this->_mtx);
    if (this->_buffer.size() > 0) {
      *dst = std::move(this->_buffer.front());
      this->_buffer.pop();
      if (T x; this->_delegate(this->_senders, &x)) {
        this->_buffer.push(std::move(x));
      }
      return true;
    }
    if (this->_delegate(this->_senders, dst)) {
      return true;
    }
    this->_recievers.push(std::move(reciever));
    return false;
  }

 private:
  const size_t _capacity;
  Spinlock _mtx;
  std::queue<T> _buffer;
  std::queue<Delegator> _senders, _recievers;

  bool _delegate(std::queue<Delegator>& delegators, T* p) {
    while (!delegators.empty()) {
      auto delegator = std::move(delegators.front());
      delegators.pop();
      if (delegator(p)) {
        return true;
      }
    }
    return false;
  }
};

}  // namespace cgo::_impl::_chan

namespace cgo {

template <typename T>
class Channel {
 public:
  Channel(size_t capacity = 0) : _impl(std::make_shared<_impl::_chan::ChannelImpl<T>>(capacity)) {}

  Coroutine<void> send(const T& x) {
    T x_copy = x;
    Semaphore sem(0);
    bool ok = _impl->send(&x_copy, [&x_copy, &sem](T* dst) {
      *dst = std::move(x_copy);
      sem.release();
      return true;
    });
    if (!ok) {
      co_await sem.aquire();
    }
  }

  Coroutine<void> send(T&& x) {
    Semaphore sem(0);
    bool ok = _impl->send(&x, [&x, &sem](T* dst) {
      *dst = std::move(x);
      sem.release();
      return true;
    });
    if (!ok) {
      co_await sem.aquire();
    }
  }

  Coroutine<T> recv() {
    Semaphore sem(0);
    T x;
    bool ok = _impl->recv(&x, [&x, &sem](T* src) {
      x = std::move(*src);
      sem.release();
      return true;
    });
    if (!ok) {
      co_await sem.aquire();
    }
    co_return std::move(x);
  }

 private:
  std::shared_ptr<_impl::_chan::ChannelImpl<T>> _impl;
};

}  // namespace cgo
