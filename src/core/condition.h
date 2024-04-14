#pragma once

#include <any>
#include <list>
#include <optional>

#include "./scheduler.h"

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
  std::suspend_always wait();
  void notify();
  size_t size() const { return this->_ptask_waitings.size(); }

 private:
  cgo::util::SpinLock* _mutex;
  WaitingSet _ptask_waitings;
};

}  // namespace cgo::_impl

namespace cgo {

class Mutex {
 public:
  Mutex() : _mptr(std::make_shared<Member>()) {}
  Coroutine<void> lock();
  void unlock();

 private:
  struct Member {
    util::SpinLock mutex;
    _impl::Condition cond;
    bool lock_flag = false;

    Member() : mutex(), cond(mutex) {}
  };

 private:
  std::shared_ptr<Member> _mptr;
};

template <typename T>
class Channel {
 public:
  Channel(size_t capacity = 0) : _mptr(std::make_shared<Member>(capacity)) {}

  Coroutine<T> recv() {
    while (true) {
      _mptr->mutex.lock();
      if (_mptr->buffer.size() > 0) {
        T res = this->_do_recv();
        _mptr->mutex.unlock();
        co_return std::move(res);
      } else {
        _mptr->w_cond.notify();
        co_await _mptr->r_cond.wait();
      }
    }
  }

  Coroutine<void> send(T&& value) {
    while (true) {
      _mptr->mutex.lock();

      // has buffer and not full
      bool ok1 = _mptr->capacity > 0 && _mptr->buffer.size() < _mptr->capacity;

      // no buffer but has waiting readers
      bool ok2 = _mptr->capacity == 0 && _mptr->buffer.empty() && _mptr->r_cond.size() > 0;

      if (ok1 || ok2) {
        this->_do_send(std::move(value));
        _mptr->mutex.unlock();
        co_return;
      } else {
        co_await _mptr->w_cond.wait();
      }
    }
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
  struct Member {
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

}  // namespace cgo
