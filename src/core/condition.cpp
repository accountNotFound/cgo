#include "./condition.h"

#include "aio/atime.h"

namespace cgo::_impl {

void WaitingSet::put(ScheduleTask* ptask) {
  if (!this->_ptask_waiting_hash.contains(ptask)) {
    this->_ptask_waiting_list.push_back(ptask);
    this->_ptask_waiting_hash[ptask] = --this->_ptask_waiting_list.end();
  }
}

ScheduleTask* WaitingSet::pop(ScheduleTask* ptask) {
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

std::suspend_always Condition::wait(size_t index, unsigned long long timeout_ms) {
  ScheduleTask* current = ScheduleTask::current;
  current->schedule_callback = [this, current, index, timeout_ms]() {
    this->_waiting_sets[index].put(current);

    if (timeout_ms > 0) {
      cgo::spawn([](std::shared_ptr<Condition> this_, ScheduleTask* current, size_t index,
                    unsigned long long timeout_ms) -> Coroutine<void> {
        co_await cgo::sleep(timeout_ms);
        {
          std::unique_lock guard(*this_);
          current = this_->_waiting_sets[index].pop(current);
        }
        if (current) {
          ScheduleContext::current->put(current);
        }
      }(this->shared_from_this(), current, index, timeout_ms));
    }

    this->_mutex.unlock();
  };
  return {};
}

void Condition::notify(size_t index) {
  ScheduleTask* ptask = this->_waiting_sets[index].pop();
  if (ptask) {
    ScheduleContext::current->put(ptask);
  }
}

}  // namespace cgo::_impl

namespace cgo {

Coroutine<void> Mutex::lock() {
  while (true) {
    this->_cond->lock();
    if (*this->_lock_flag) {
      co_await this->_cond->wait(0);
    } else {
      *this->_lock_flag = true;
      this->_cond->unlock();
      break;
    }
  }
}

void Mutex::unlock() {
  std::unique_lock guard(*this->_cond);
  *this->_lock_flag = false;
  this->_cond->notify(0);
}

Coroutine<int> Selector::recv() {
  for (auto& [_, listen] : this->_listeners) {
    listen();
  }
  while (true) {
    int key = co_await this->_active_chan.recv();
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
  std::optional<int> opt = this->_active_chan.recv_nowait();
  if (opt.has_value() && this->_active_callbacks[*opt]()) {
    this->_done_flag->store(true);
    co_return std::move(*opt);
  } else {
    this->_done_flag->store(true);
    co_return std::move(key);
  }
}

}  // namespace cgo
