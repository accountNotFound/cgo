#include "core/schedule.h"

namespace cgo {

void Spinlock::lock() {
  bool expected = false;
  while (!_flag.compare_exchange_weak(expected, true)) {
    expected = false;
  }
}

namespace _impl::_sched {

TaskHandler TaskAllocator::create(int id, Coroutine<void> fn) {
  fn.init();
  {
    std::unique_lock guard(this->_mtx);
    this->_index[id] = this->_pool.emplace(this->_pool.end(), id, std::move(fn));
  }
  return &*this->_index[id];
}

void TaskAllocator::destroy(TaskHandler task) {
  int id = task.id();
  {
    std::unique_lock guard(this->_mtx);
    this->_pool.erase(this->_index.at(id));
    this->_index.erase(id);
  }
}

void TaskQueue::push(TaskHandler task) {
  std::unique_lock guard(this->_mtx);
  this->_que.push(task);
}

TaskHandler TaskQueue::pop() {
  std::unique_lock guard(this->_mtx);
  if (this->_que.empty()) {
    return nullptr;
  }
  auto task = this->_que.front();
  this->_que.pop();
  return task;
}

std::suspend_always TaskExecutor::yield_running_task() {
  get_dispatcher().submit(TaskExecutor::_t_running);
  TaskExecutor::_t_running = nullptr;
  return {};
}

std::suspend_always TaskExecutor::suspend_running_task(TaskQueue& q_waiting) {
  q_waiting.push(TaskExecutor::_t_running);
  TaskExecutor::_t_running = nullptr;
  return {};
}

void TaskExecutor::execute(TaskHandler task) {
  TaskExecutor::_t_running = task;
  while (TaskExecutor::_t_running && !TaskExecutor::_t_running.done()) {
    TaskExecutor::_t_running.resume();
  }
  if (TaskExecutor::_t_running) {
    get_dispatcher().destroy(TaskExecutor::_t_running);
  }
}

Coroutine<void> TaskCondition::wait(std::unique_lock<Spinlock>& lock) {
  TaskExecutor::suspend_running_task(this->_q_waiting);
  lock.unlock();
  co_await std::suspend_always{};
  lock.lock();
}

void TaskCondition::notify() {
  if (auto next = this->_q_waiting.pop(); next) {
    get_dispatcher().submit(next);
  }
}

void TaskDispatcher::create(Coroutine<void> fn) {
  int id = this->_tid.fetch_add(1);
  int slot = id % this->_t_allocs.size();
  auto task = this->_t_allocs[slot].create(id, std::move(fn));
  this->_q_runnables[slot].push(task);
}

void TaskDispatcher::destroy(TaskHandler task) {
  int slot = task.id() % this->_t_allocs.size();
  this->_t_allocs[slot].destroy(task);
}

void TaskDispatcher::submit(TaskHandler task) {
  int slot = task.id() % this->_q_runnables.size();
  this->_q_runnables[slot].push(task);
}

TaskHandler TaskDispatcher::dispatch(size_t p_index) {
  TaskHandler task = nullptr;
  for (int i = 0; !task && i < this->_q_runnables.size(); i++) {
    int slot = (p_index + i) % this->_q_runnables.size();
    task = this->_q_runnables[slot].pop();
  }
  return task;
}

}  // namespace _impl::_sched

Coroutine<void> Semaphore::aquire() {
  std::unique_lock guard(_self->mtx);
  while (true) {
    if (_self->vacant > 0) {
      _self->vacant--;
      co_return;
    }
    co_await _self->cond.wait(guard);
  }
}

void Semaphore::release() {
  {
    std::unique_lock guard(_self->mtx);
    _self->vacant++;
  }
  _self->cond.notify();
}

}  // namespace cgo
