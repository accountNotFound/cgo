#include "./context.h"

namespace cgo::_impl {

void ScheduleContext::allocate(ScheduleTask&& task) {
  std::unique_lock storage_guard(this->_mutex);
  auto [it, ok] = this->_task_storage.emplace(std::move(task));
  this->_ptask_runnings.push(const_cast<ScheduleTask*>(&*it));
}

void ScheduleContext::destroy(ScheduleTask* ptask) {
  std::unique_lock guard(this->_mutex);
  this->_task_storage.erase(*ptask);
}

ScheduleTask* ScheduleContext::get() {
  std::unique_lock guard(this->_mutex);
  if (this->_ptask_runnings.empty()) {
    return nullptr;
  }
  ScheduleTask* res = this->_ptask_runnings.front();
  this->_ptask_runnings.pop();
  return res;
}

void ScheduleContext::put(ScheduleTask* ptask) {
  std::unique_lock guard(this->_mutex);
  this->_ptask_runnings.push(ptask);
}

}  // namespace cgo::_impl

namespace cgo {

std::suspend_always yield() {
  _impl::ScheduleTask* ptask = _impl::ScheduleTask::current;
  ptask->schedule_callback = [ptask]() { _impl::ScheduleContext::current->put(ptask); };
  return {};
}

}  // namespace cgo
