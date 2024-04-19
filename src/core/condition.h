#pragma once

#include <any>
#include <list>
#include <optional>

#include "./error.h"
#include "./scheduler.h"
#include "./timer.h"

namespace cgo::_impl {

class WaitingSet {
 public:
  void put(Task* ptask);
  Task* pop(Task* ptask = nullptr);
  size_t size() const { return this->_ptask_waiting_hash.size(); }

 private:
  std::list<Task*> _ptask_waiting_list;
  std::unordered_map<Task*, std::list<Task*>::iterator> _ptask_waiting_hash;
};

class Condition {
 public:
  Condition(cgo::util::SpinLock& mutex) : _mutex(&mutex) {}
  void lock() { this->_mutex->lock(); }
  void unlock() { this->_mutex->unlock(); }

  // Arguments:
  // owner: object of this condition and mutex belongs to. Owner object should be managed by shared_ptr
  Coroutine<void> wait(std::weak_ptr<void> owner);
  void notify();
  size_t size() const { return this->_ptask_waitings.size(); }

 private:
  cgo::util::SpinLock* _mutex;
  WaitingSet _ptask_waitings;
};

}  // namespace cgo::_impl

namespace cgo {

// reference type. Can be shared across coroutines
class Mutex {
 public:
  Mutex() : _mptr(std::make_shared<Member>()) {}
  Coroutine<void> lock();
  void unlock();

 private:
  struct Member : public std::enable_shared_from_this<Member> {
    util::SpinLock mutex;
    _impl::Condition cond;
    bool lock_flag = false;

    Member() : mutex(), cond(mutex) {}
  };

 private:
  std::shared_ptr<Member> _mptr;
};

// reference type. Can be shared across coroutines
template <typename T>
class Channel {
 public:
  Channel(size_t capacity = 0) : _mptr(std::make_shared<Member>(capacity)) {}

  Coroutine<T> recv() {
    std::unique_lock guard(_mptr->mutex);
    while (true) {
      if (_mptr->buffer.size() > 0) {
        T res = this->_do_recv();
        co_return std::move(res);
      } else {
        _mptr->w_cond.notify();
        co_await _mptr->r_cond.wait(_mptr->weak_from_this());
      }
    }
  }

  Coroutine<void> send(T&& value) {
    std::unique_lock guard(_mptr->mutex);
    while (true) {

      // has buffer and not full
      bool ok1 = _mptr->capacity > 0 && _mptr->buffer.size() < _mptr->capacity;

      // no buffer but has waiting readers
      bool ok2 = _mptr->capacity == 0 && _mptr->buffer.empty() && _mptr->r_cond.size() > 0;

      if (ok1 || ok2) {
        this->_do_send(std::move(value));
        co_return;
      } else {
        co_await _mptr->w_cond.wait(_mptr->weak_from_this());
      }
    }
  }

  Coroutine<bool> test() {
    std::unique_lock guard(_mptr->mutex);
    for (int i = 0; i < 2; i++) {
      if (!_mptr->buffer.empty()) {
        _mptr->r_cond.notify();
        co_return true;
      } else {
        _mptr->w_cond.notify();
        co_await _mptr->r_cond.wait(_mptr->weak_from_this());  // unlock here
      }
    }
    co_return false;
  }

  bool send_nowait(T&& value) {
    std::unique_lock guard(_mptr->mutex);
    bool ok1 = _mptr->capacity > 0 && _mptr->buffer.size() < _mptr->capacity;
    bool ok2 = _mptr->capacity == 0 && _mptr->buffer.empty() && _mptr->r_cond.size() > 0;

    if (!ok1 && !ok2) {
      return false;
    }
    this->_do_send(std::move(value));
    return true;
  }

  std::optional<T> recv_nowait() {
    std::unique_lock guard(_mptr->mutex);
    if (!_mptr->buffer.empty()) {
      return std::make_optional<T>(this->_do_recv());
    }
    return std::nullopt;
  }

 private:
  void _do_send(T&& value) {
    _mptr->buffer.push(std::move(value));
    _mptr->r_cond.notify();
  }
  T _do_recv() {
    T res = std::move(_mptr->buffer.front());
    _mptr->buffer.pop();
    _mptr->w_cond.notify();
    return res;
  }

 private:
  struct Member : public std::enable_shared_from_this<Member> {
    util::SpinLock mutex;
    _impl::Condition r_cond;
    _impl::Condition w_cond;
    std::queue<T> buffer;
    size_t capacity = 0;

    Member(size_t capacity) : mutex(), r_cond(mutex), w_cond(mutex), capacity(capacity) {}
  };

 private:
  std::shared_ptr<Member> _mptr;
};

class Selector {
 public:
  Selector() : _active_keys(INT_MAX), _done_flag(std::make_shared<std::atomic<bool>>(false)) {}
  Selector(const Selector&) = delete;
  Selector(Selector&&) = delete;

  template <typename T>
  Selector& on(int key, Channel<T> chan, int64_t interval_ms = 1e4) {
    this->_listeners[key] = [this, key, chan, interval_ms]() {
      spawn([](int key, Channel<T> value_chan, Channel<int> notify_chan, std::shared_ptr<std::atomic<bool>> done_flag,
               int64_t interval_ms) -> Coroutine<void> {
        while (true) {
          try {
            if (co_await timeout(value_chan.test(), interval_ms)) {
              notify_chan.send_nowait(std::move(key));
              break;
            }
          } catch (const TimeoutException& e) {
            if (done_flag->load()) {
              break;
            }
          }
        }
      }(key, chan, this->_active_keys, this->_done_flag, interval_ms));
    };
    this->_active_callbacks[key] = [this, chan]() mutable -> bool {
      std::optional<T> opt = chan.recv_nowait();
      if (opt.has_value()) {
        this->_active_value = std::make_any<T>(std::move(*opt));
        return true;
      }
      return false;
    };
    return *this;
  }

  template <typename T>
  T cast() {
    if constexpr (std::is_same_v<T, std::any>) {
      return std::move(this->_active_value);
    }
    return std::any_cast<T>(std::move(this->_active_value));
  }

  Coroutine<int> recv();
  Coroutine<int> recv_or_default(int key);

 private:
  std::unordered_map<int, std::function<void()>> _listeners;
  std::unordered_map<int, std::function<bool()>> _active_callbacks;
  Channel<int> _active_keys;
  std::shared_ptr<std::atomic<bool>> _done_flag;
  std::any _active_value;
};

}  // namespace cgo
