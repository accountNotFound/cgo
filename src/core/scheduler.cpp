#include "./scheduler.h"

namespace cgo::_impl {

void Scheduler::allocate(Task&& task) {
  std::unique_lock storage_guard(this->_mutex);
  auto [it, ok] = this->_task_storage.emplace(std::move(task));
  this->_ptask_runnings.push(const_cast<Task*>(&*it));
}

void Scheduler::destroy(Task* ptask) {
  std::unique_lock guard(this->_mutex);
  this->_task_storage.erase(*ptask);
}

Task* Scheduler::get() {
  std::unique_lock guard(this->_mutex);
  if (this->_ptask_runnings.empty()) {
    return nullptr;
  }
  Task* res = this->_ptask_runnings.front();
  this->_ptask_runnings.pop();
  return res;
}

void Scheduler::put(Task* ptask) {
  std::unique_lock guard(this->_mutex);
  this->_ptask_runnings.push(ptask);
  ptask->await_timeout_ms = -1;
}

}  // namespace cgo::_impl

namespace cgo {

Coroutine<void> yield() {
  _impl::Task* ptask = _impl::Task::current;
  ptask->schedule_callback = [ptask]() { _impl::Scheduler::current->put(ptask); };
  co_await std::suspend_always{};
}

}  // namespace cgo
