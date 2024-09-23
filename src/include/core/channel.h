#pragma once

#include <functional>
#include <random>
#include <vector>

#include "core/schedule.h"

namespace cgo::_impl::_chan {

class ValueBase : public std::enable_shared_from_this<ValueBase> {
 public:
  /**
   * @brief The returned spinlock will be used to protected `data()` ptr, if returned value is not null
   */
  virtual Spinlock* mutex() { return nullptr; }

  /**
   * @brief Pointer to be read or write by move semantic. Return nullptr if this object is expired
   * @note Don't lock/unlock `mutex()` inside this method. Mutex will be lock if necessary inside channel
   */
  virtual void* data() { return nullptr; }

  /**
   * @brief This method will be called once send/write/close event happend
   * @param success: True if the `data()` ptr is consumed/overwrote. Or false if channel is closed
   */
  virtual void commit(bool success) {};
};

class ChannelBase {
 public:
  ChannelBase(size_t capacity) : _capacity(capacity) {}

  bool send(std::shared_ptr<ValueBase>& src);

  bool recv(std::shared_ptr<ValueBase>& dst);

 protected:
  /**
   * @brief Move semantic
   */
  virtual void _send_to_buffer_impl(void* src) = 0;

  /**
   * @brief Move semantic
   */
  virtual void _recv_from_buffer_impl(void* dst) = 0;

  /**
   * @brief Move semantic
   */
  virtual void _transmit_impl(void* src, void* dst) = 0;

 private:
  const size_t _capacity;
  Spinlock _mtx;
  std::queue<std::weak_ptr<ValueBase>> _senders, _recievers;
  size_t _size = 0;

  bool _send_to_buffer(std::shared_ptr<ValueBase>& src);

  bool _recv_from_buffer(std::shared_ptr<ValueBase>& dst);

  bool _send_to_reciever(std::shared_ptr<ValueBase>& src);

  bool _recv_from_sender(std::shared_ptr<ValueBase>& dst);

  bool _transmit(std::shared_ptr<ValueBase>& src, std::shared_ptr<ValueBase>& dst);
};

template <typename T>
class ChannelImpl : public ChannelBase {
 public:
  ChannelImpl(size_t capacity) : ChannelBase(capacity) {}

 private:
  std::queue<T> _buffer;

  void _send_to_buffer_impl(void* src) override { this->_buffer.push(std::move(*static_cast<T*>(src))); }

  void _recv_from_buffer_impl(void* dst) override {
    *static_cast<T*>(dst) = std::move(this->_buffer.front());
    this->_buffer.pop();
  }

  void _transmit_impl(void* src, void* dst) override { *static_cast<T*>(dst) = std::move(*static_cast<T*>(src)); }
};

}  // namespace cgo::_impl::_chan

namespace cgo {

template <typename T>
class Channel {
 public:
  Channel(size_t capacity = 0) : _impl(std::make_shared<_impl::_chan::ChannelImpl<T>>(capacity)) {}

  Coroutine<bool> operator<<(const T& x) {
    T x_copy = x;
    Value value(&x_copy);
    std::shared_ptr<_impl::_chan::ValueBase> src(&value, [](auto* p) {});
    if (!this->_impl->send(src)) {
      co_await value.sem.aquire();
    }
    co_return std::move(value.ok);
  }

  Coroutine<bool> operator<<(T&& x) {
    Value value(&x);
    std::shared_ptr<_impl::_chan::ValueBase> src(&value, [](auto* p) {});
    if (!this->_impl->send(src)) {
      co_await value.sem.aquire();
    }
    co_return std::move(value.ok);
  }

  Coroutine<bool> operator>>(T& x) {
    Value value(&x);
    std::shared_ptr<_impl::_chan::ValueBase> dst(&value, [](auto* p) {});
    if (!this->_impl->recv(dst)) {
      co_await value.sem.aquire();
    }
    co_return std::move(value.ok);
  }

 private:
  struct Value : public _impl::_chan::ValueBase {
   public:
    T* ptr;
    Semaphore sem = {0};
    bool ok = false;

    Value(T* data) : ptr(data) {}

    void* data() override { return this->ptr; }

    void commit(bool success) override {
      this->ok = success;
      this->sem.release();
    }
  };

  std::shared_ptr<_impl::_chan::ChannelImpl<T>> _impl;
};

}  // namespace cgo
