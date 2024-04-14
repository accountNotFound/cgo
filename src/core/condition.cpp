#include "./condition.h"

#include "./error.h"
#include "./timer.h"

namespace cgo::_impl {

void WaitingSet::put(Task* ptask) {
  if (!this->_ptask_waiting_hash.contains(ptask)) {
    this->_ptask_waiting_list.push_back(ptask);
    this->_ptask_waiting_hash[ptask] = --this->_ptask_waiting_list.end();
  }
}

Task* WaitingSet::pop(Task* ptask) {
  if (!ptask && !this->_ptask_waiting_hash.empty()) {
    ptask = this->_ptask_waiting_list.front();
  }
  if (this->_ptask_waiting_hash.contains(ptask)) {
    this->_ptask_waiting_list.erase(this->_ptask_waiting_hash[ptask]);
    this->_ptask_waiting_hash.erase(ptask);
    return ptask;
  }
  return nullptr;
}

Coroutine<void> Condition::wait(std::weak_ptr<void> weak_owner) {
  Task* current = Task::current;
  current->schedule_callback = [this, current]() {
    this->_ptask_waitings.put(current);
    this->_mutex->unlock();
  };
  if (current->await_timeout_ms != -1) {
    Timer::current->add(
        [current, this, weak_owner]() mutable {
          auto shared_owner = weak_owner.lock();
          if (!shared_owner) {
            return;
          }
          {
            std::unique_lock guard(*this);
            current = this->_ptask_waitings.pop(current);
          }
          if (current) {
            current->cancel(std::make_exception_ptr(TimeoutException(current->await_timeout_ms)));
            Scheduler::current->put(current);
          }
        },
        current->await_timeout_ms);
  }
  co_await std::suspend_always{};
}

void Condition::notify() {
  Task* ptask = this->_ptask_waitings.pop();
  if (ptask) {
    Scheduler::current->put(ptask);
  }
}

}  // namespace cgo::_impl

namespace cgo {

Coroutine<void> Mutex::lock() {
  while (true) {
    _mptr->cond.lock();
    if (_mptr->lock_flag) {
      co_await _mptr->cond.wait(_mptr->weak_from_this());
    } else {
      _mptr->lock_flag = true;
      _mptr->cond.unlock();
      break;
    }
  }
}

void Mutex::unlock() {
  std::unique_lock guard(_mptr->cond);
  _mptr->lock_flag = false;
  _mptr->cond.notify();
}

Coroutine<int> Selector::recv() {
  for (auto& [_, listen] : this->_listeners) {
    listen();
  }
  while (true) {
    int key = co_await this->_active_keys.recv();
    if (this->_active_callbacks[key]()) {
      this->_done_flag->store(true);
      co_return std::move(key);
    } else {
      this->_listeners[key]();
    }
  }
}

Coroutine<int> Selector::recv_or_default(int key) {
  for (auto& [_, listen] : this->_listeners) {
    listen();
  }
  std::optional<int> opt = this->_active_keys.recv_nowait();
  if (opt.has_value() && this->_active_callbacks[*opt]()) {
    this->_done_flag->store(true);
    co_return std::move(*opt);
  } else {
    this->_done_flag->store(true);
    co_return std::move(key);
  }
}

}  // namespace cgo
