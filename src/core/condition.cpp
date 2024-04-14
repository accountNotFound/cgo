#include "./condition.h"

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

std::suspend_always Condition::wait() {
  Task* current = Task::current;
  current->schedule_callback = [this, current]() {
    this->_ptask_waitings.put(current);

    // TODO: wait with timeout

    this->_mutex->unlock();
  };
  return {};
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
      co_await _mptr->cond.wait();
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

}  // namespace cgo
