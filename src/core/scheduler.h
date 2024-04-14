#pragma once

#include <functional>
#include <queue>
#include <unordered_set>

#include "../util/slock.h"
#include "./coroutine.h"

namespace cgo::_impl {

class Task {
 public:
  static thread_local Task* current;

 public:
  struct Hash {
    size_t operator()(const Task& task) const { return task.id(); }
  };

 public:
  template <typename T>
  Task(Coroutine<T>&& target) : _target(std::make_unique<Coroutine<T>>(std::move(target))) {
    this->_target->start();
  }

  void resume() { this->_target->resume(); }
  bool done() { return this->_target->done(); }
  void cancel(std::exception_ptr err) { this->_target->promise().co_frames->top()->error = err; }

  size_t id() const { return reinterpret_cast<size_t>(this->_target.get()); }
  bool operator==(const Task& rhs) const { return this->id() == rhs.id(); }

 public:
  std::function<void()> schedule_callback = nullptr;
  int64_t await_timeout_ms = -1;

 private:
  std::unique_ptr<CoroutineBase> _target;
};

class Scheduler {
 public:
  static thread_local Scheduler* current;

 public:
  Scheduler() { Scheduler::current = this; }

  void allocate(Task&& task);
  void destroy(Task* ptask);
  Task* get();
  void put(Task* ptask);

 private:
  cgo::util::SpinLock _mutex;
  std::unordered_set<Task, Task::Hash> _task_storage;
  std::queue<Task*> _ptask_runnings;
};

}  // namespace cgo::_impl

namespace cgo {

template <typename T>
void spawn(Coroutine<T>&& target) {
  _impl::Scheduler::current->allocate(_impl::Task(std::move(target)));
}

Coroutine<void> yield();

}  // namespace cgo