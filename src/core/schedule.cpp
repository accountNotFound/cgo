#include "core/schedule.h"

namespace cgo {

void Spinlock::lock() {
  bool expected = false;
  while (!this->_flag.compare_exchange_weak(expected, true)) {
    expected = false;
  }
}

namespace _impl {

void SignalBase::emit() {
  if (this->_wait_flag && !this->_signal_flag) {
    std::unique_lock guard(this->_mtx);
    if (this->_wait_flag && !this->_signal_flag) {
      this->_cond.notify_one();
      this->_signal_flag = true;
    }
  }
}

void SignalBase::wait(const std::chrono::duration<double, std::milli>& duration) {
  if (!this->_signal_flag) {
    std::unique_lock guard(this->_mtx);
    if (!this->_signal_flag) {
      this->_signal_flag = false;
      this->_wait_flag = true;
      this->_cond.wait_for(guard, duration);
      this->_wait_flag = false;
    }
  }
}

}  // namespace _impl

namespace _impl::_sched {

TaskHandler TaskAllocator::create(int id, Coroutine<void> fn, const std::string& name) {
  Task* task;
  {
    std::unique_lock guard(this->_mtx);
    this->_index[id] = this->_pool.emplace(this->_pool.end(), id, std::move(fn), name);
    task = &*this->_index[id];
    task->create_at = std::chrono::steady_clock::now();
  }
  _impl::_coro::init(task->fn);
  return task;
}

void TaskAllocator::destroy(TaskHandler task) {
  int id = task->id;
  {
    std::unique_lock guard(this->_mtx);
    this->_pool.erase(this->_index.at(id));
    this->_index.erase(id);
  }
}

void TaskQueue::push(TaskHandler task) {
  std::unique_lock guard(this->_mtx);
  this->_que.push(task);
  if (this->_signal) {
    this->_signal->emit();
  }
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

Coroutine<void> TaskExecutor::yield_running_task() {
  TaskExecutor::_yield_flag = true;
  co_await std::suspend_always{};
}

Coroutine<void> TaskExecutor::suspend_running_task(TaskQueue& q_waiting, std::unique_lock<Spinlock>& cond_lock) {
  TaskExecutor::_suspend_q_waiting = &q_waiting;
  auto mtx = TaskExecutor::_suspend_cond_lock = cond_lock.release();
  co_await std::suspend_always{};
  cond_lock = std::unique_lock(*mtx);
}

void TaskExecutor::execute(TaskHandler task) {
  TaskExecutor::_t_running = task;

  task->execute_cnt++;
  while (!TaskExecutor::_yield_flag && !TaskExecutor::_suspend_q_waiting && !_impl::_coro::done(task->fn)) {
    _impl::_coro::resume(task->fn);
  }

  if (TaskExecutor::_yield_flag) {
    task->yield_cnt++;

    get_dispatcher().submit(task);
    TaskExecutor::_yield_flag = false;
  } else if (TaskExecutor::_suspend_q_waiting) {
    task->suspend_cnt++;

    TaskExecutor::_suspend_q_waiting->push(task);
    TaskExecutor::_suspend_cond_lock->unlock();
    TaskExecutor::_suspend_q_waiting = nullptr;
    TaskExecutor::_suspend_cond_lock = nullptr;
  } else {
    get_dispatcher().destroy(task);
  }
  TaskExecutor::_t_running = nullptr;
}

Coroutine<void> TaskCondition::wait(std::unique_lock<Spinlock>& lock) {
  return TaskExecutor::suspend_running_task(this->_q_waiting, lock);
}

void TaskCondition::notify() {
  if (auto next = this->_q_waiting.pop(); next) {
    get_dispatcher().submit(next);
  }
}

void TaskDispatcher::create(Coroutine<void> fn, const std::string& name) {
  int id = this->_tid.fetch_add(1);
  int slot = id % this->_t_allocs.size();
  auto task = this->_t_allocs[slot].create(id, std::move(fn), name);
  this->_q_runnables[slot].push(task);
}

void TaskDispatcher::destroy(TaskHandler task) {
  int slot = task->id % this->_t_allocs.size();
  this->_t_allocs[slot].destroy(task);
}

void TaskDispatcher::submit(TaskHandler task) {
  int slot = task->id % this->_q_runnables.size();
  this->_q_runnables[slot].push(task);
}

TaskHandler TaskDispatcher::dispatch(size_t p_index) {
  TaskHandler task = nullptr;
  for (int i = 0; !task && i < this->_q_runnables.size(); ++i) {
    int slot = (p_index + i) % this->_q_runnables.size();
    task = this->_q_runnables[slot].pop();
  }
  return task;
}

}  // namespace _impl::_sched

Coroutine<void> Semaphore::aquire() {
  std::unique_lock guard(this->_mtx);
  while (true) {
    if (this->_vacant > 0) {
      this->_vacant--;
      break;
    }
    co_await this->_cond.wait(guard);
  }
}

void Semaphore::release() {
  {
    std::unique_lock guard(this->_mtx);
    this->_vacant++;
    this->_cond.notify();
  }
}

DeferGuard::DeferGuard(DeferGuard&& rhs) {
  this->drop();
  std::swap(this->_defer, rhs._defer);
}

DeferGuard::~DeferGuard() {
  if (this->_defer) {
    this->_defer();
    this->_defer = nullptr;
  }
}

}  // namespace cgo
