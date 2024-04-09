#pragma once

#include <functional>
#include <memory>
#include <queue>
#include <unordered_set>

#include "./coroutine.h"
#include "util/slock.h"

namespace cgo::_impl {

class ScheduleTask {
 public:
  static thread_local ScheduleTask* current;

 public:
  struct Hash {
    size_t operator()(const ScheduleTask& task) const { return task.id(); }
  };

 public:
  template <typename T>
  ScheduleTask(Coroutine<T>&& target) : _target(std::make_unique<Coroutine<T>>(std::move(target))) {
    this->_target->start();
  }

  void resume() { this->_target->resume(); }
  bool done() { return this->_target->done(); }
  size_t id() const { return reinterpret_cast<size_t>(this->_target.get()); }
  bool operator==(const ScheduleTask& rhs) const { return this->id() == rhs.id(); }

 public:
  std::function<void()> schedule_callback = nullptr;

 private:
  std::unique_ptr<CoroutineBase> _target;
};

class ScheduleContext {
 public:
  static thread_local ScheduleContext* current;

 public:
  ScheduleContext() { ScheduleContext::current = this; }

  void allocate(ScheduleTask&& task);
  void destroy(ScheduleTask* ptask);
  ScheduleTask* get();
  void put(ScheduleTask* ptask);

 private:
  cgo::util::SpinLock _mutex;
  std::unordered_set<ScheduleTask, ScheduleTask::Hash> _task_storage;
  std::queue<ScheduleTask*> _ptask_runnings;
};

}  // namespace cgo::_impl

namespace cgo {

std::suspend_always yield();

template <typename T>
void spawn(Coroutine<T>&& target) {
  _impl::ScheduleContext::current->allocate(_impl::ScheduleTask(std::move(target)));
}

}  // namespace cgo