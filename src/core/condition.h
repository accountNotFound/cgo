#pragma once

#include <any>
#include <list>
#include <optional>

#include "./context.h"

namespace cgo::_impl {

class WaitingSet {
 public:
  void put(ScheduleTask* ptask);
  ScheduleTask* pop(ScheduleTask* ptask = nullptr);
  size_t size() const { return this->_ptask_waiting_hash.size(); }

 private:
  std::list<ScheduleTask*> _ptask_waiting_list;
  std::unordered_map<ScheduleTask*, std::list<ScheduleTask*>::iterator> _ptask_waiting_hash;
};

class Condition : public std::enable_shared_from_this<Condition> {
 public:
  Condition(size_t user_type_num) : _waiting_sets(user_type_num) {}

  void lock() { this->_mutex.lock(); }
  void unlock() { this->_mutex.unlock(); }
  std::suspend_always wait(size_t index, unsigned long long timeout_ms = 0);
  void notify(size_t index);
  size_t size(size_t index) { return this->_waiting_sets[index].size(); }

 private:
  util::SpinLock _mutex;
  std::vector<WaitingSet> _waiting_sets;
};

}  // namespace cgo::_impl

namespace cgo {

class ReferenceType {};

class Mutex : public ReferenceType {
 public:
  Mutex() : _cond(std::make_shared<_impl::Condition>(1)), _lock_flag(std::make_shared<bool>(false)) {}

  Coroutine<void> lock();
  void unlock();

 private:
  std::shared_ptr<_impl::Condition> _cond;
  std::shared_ptr<bool> _lock_flag;
};

template <typename T>
class Channel : public ReferenceType {
  enum UserType { READER = 0, WRITER = 1 };

 public:
  Channel(size_t capacity = 0)
      : _cond(std::make_shared<_impl::Condition>(2)), _buffer(std::make_shared<std::queue<T>>()), _capacity(capacity) {}

  // return true if channel is not empty. This method will eventually return after `timeout_ms` if `timeout_ms>0`
  Coroutine<bool> test(unsigned long long timeout_ms = 0) {
    auto lock_and_test = [this]() {
      this->_cond->lock();
      return this->_buffer->size() > 0;
    };

    if (lock_and_test()) {
      this->_cond->notify(UserType::READER);
      this->_cond->unlock();
      co_return true;
    } else {
      this->_cond->notify(UserType::WRITER);
      co_await this->_cond->wait(UserType::READER, timeout_ms);  // unlock here
    }

    if (lock_and_test()) {
      this->_cond->notify(UserType::READER);
      this->_cond->unlock();
      co_return true;
    } else {
      this->_cond->notify(UserType::WRITER);
      this->_cond->unlock();
      co_return false;
    }
  }

  Coroutine<T> recv() {
    while (true) {
      this->_cond->lock();
      if (this->_buffer->size() > 0) {
        T res = this->_do_recv();
        this->_cond->unlock();
        co_return std::move(res);
      } else {
        this->_cond->notify(UserType::WRITER);
        co_await this->_cond->wait(UserType::READER);
      }
    }
  }

  Coroutine<void> send(T&& value) {
    while (true) {
      this->_cond->lock();

      // has buffer and not full
      bool ok1 = this->_capacity > 0 && this->_buffer->size() < this->_capacity;

      // no buffer but has waiting readers
      bool ok2 = this->_capacity == 0 && this->_buffer->empty() && this->_cond->size(UserType::READER) > 0;

      if (ok1 || ok2) {
        this->_do_send(std::move(value));
        this->_cond->unlock();
        co_return;
      } else {
        co_await this->_cond->wait(UserType::WRITER);
      }
    }
  }

  std::optional<T> recv_nowait() {
    std::unique_lock guard(*this->_cond);
    if (this->_buffer->size() > 0) {
      return std::make_optional<T>(this->_do_recv());
    }
    return std::nullopt;
  }

  bool send_nowait(T&& value) {
    std::unique_lock guard(*this->_cond);
    bool ok1 = this->_capacity > 0 && this->_buffer->size() < this->_capacity;
    bool ok2 = this->_capacity == 0 && this->_buffer->empty() && this->_cond->size(UserType::READER) > 0;

    if (!ok1 && !ok2) {
      return false;
    }
    this->_do_send(std::move(value));
    return true;
  }

  size_t id() const { return reinterpret_cast<size_t>(this->_buffer.get()); }

  size_t use_count() const { return this->_buffer.use_count(); }

 private:
  void _do_send(T&& value) {
    this->_buffer->push(std::move(value));
    this->_cond->notify(UserType::READER);
  }
  T _do_recv() {
    T res = std::move(this->_buffer->front());
    this->_buffer->pop();
    this->_cond->notify(UserType::WRITER);
    return res;
  }

 private:
  std::shared_ptr<_impl::Condition> _cond;
  std::shared_ptr<std::queue<T>> _buffer;
  size_t _capacity;
};

class Selector {
 public:
  Selector() : _potential_events(INT_MAX) {}
  Selector(const Selector&) = delete;
  Selector(Selector&&) = delete;

  template <typename T>
  bool test(Channel<T>& chan) {
    auto listen = [this, &chan]() {
      cgo::spawn([](Channel<T> value_chan, Channel<size_t> event_chan) -> Coroutine<void> {
        const unsigned long long test_timeout_ms = 60 * 1000;  // 1 minute
        while (true) {
          if (co_await value_chan.test(test_timeout_ms)) {
            event_chan.send_nowait(value_chan.id());
            break;
          } else if (value_chan.use_count() == 1) {
            // the value_chan is dead, end this coroutine
            break;
          }
        }
      }(chan, this->_potential_events));
    };
    auto touch = [this, &chan]() -> bool {
      std::optional<T> opt = chan.recv_nowait();
      if (opt.has_value()) {
        this->_value = std::make_any<T>(std::move(*opt));
        return true;
      }
      return false;
    };

    this->_event_listeners[chan.id()] = listen;
    if (this->_current_event != 0 && this->_current_event != chan.id()) {
      // only check the channel whose id=this->_current_event
      return false;
    }
    // try receive from target channel. Or try receive from every channel in first scan loop
    return touch();
  }

  template <typename T>
  T cast() {
    if constexpr (std::is_same_v<T, std::any>) {
      return std::move(this->_value);
    }
    return std::any_cast<T>(std::move(this->_value));
  }

  // return an acitavated channel's id
  Coroutine<size_t> wait();

 private:
  std::any _value;
  std::unordered_map<size_t, std::function<void()>> _event_listeners;
  Channel<size_t> _potential_events;
  size_t _current_event = 0;
};

template <typename T, typename ChanT = std::conditional<std::is_same_v<T, void>, bool, T>::type>
Channel<ChanT> collect(Coroutine<T>&& target) {
  Channel<ChanT> chan(1);
  _impl::ScheduleTask task = [](Coroutine<T> target, Channel<ChanT> chan) -> Coroutine<void> {
    if constexpr (std::is_same_v<T, void>) {
      co_await target;
      co_await chan.send(true);
    } else {
      T res = co_await target;
      co_await chan.send(std::move(res));
    }
  }(std::move(target), chan);
  _impl::ScheduleContext::current->allocate(std::move(task));
  return chan;
}

}  // namespace cgo
