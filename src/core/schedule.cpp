#include "core/schedule.h"

namespace cgo {

namespace _impl {

void Spinlock::lock() {
  bool expected = false;
  while (!this->_lock_flag.compare_exchange_weak(expected, true)) {
    expected = false;
  }
}

void Spinlock::unlock() { this->_lock_flag = false; }

void LazySignalBase::emit() {
  if (this->_wait_flag && !this->_signal_flag) {
    std::unique_lock guard(this->_mtx);
    if (this->_wait_flag && !this->_signal_flag) {
      this->_cond.notify_one();
      this->_signal_flag = true;
    }
  }
}

void LazySignalBase::wait(std::chrono::duration<double, std::milli> duration) {
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

namespace _sched {

auto TaskAllocator::create(Context* ctx, int id, Coroutine<void> fn, std::string&& name) -> TaskAllocator::Handler {
  Task* task;
  {
    std::unique_lock guard(this->_mtx);
    this->_index[id] = this->_pool.emplace(this->_pool.end(), ctx, id, std::move(fn), std::forward<std::string>(name));
    task = &*this->_index[id];
  }
  static_cast<_impl::CoroutineBase&>(task->fn).init();
  return Handler(task);
}

void TaskAllocator::destroy(TaskAllocator::Handler task) {
  int id = task->id;
  {
    std::unique_lock guard(this->_mtx);
    this->_pool.erase(this->_index.at(id));
    this->_index.erase(id);
  }
}

void TaskAllocator::clear() {
  for (auto& task : this->_pool) {
    static_cast<_impl::CoroutineBase&>(task.fn).destroy();
  }
}

Coroutine<void> TaskCondition::wait(std::unique_lock<Spinlock>& guard) {
  co_await TaskExecutor::wait(&this->_blocked_tasks, guard.mutex());
}

void TaskCondition::notify() {
  if (auto next = this->_blocked_tasks.pop(); next) {
    auto& task = *next.value().get();
    Partition<TaskProcessor>::get(task.ctx).scheduler(task.id).push(std::move(*next));
  }
}

void TaskScheduler::push(TaskAllocator::Handler task) {
  std::unique_lock guard(this->_mtx);
  this->_que.push(std::move(task));
  if (this->_signal) {
    this->_signal->emit();
  }
}

Coroutine<void> TaskExecutor::yield() {
  TaskExecutor::_yield_flag = true;
  co_await std::suspend_always{};
}

Coroutine<void> TaskExecutor::wait(TaskCondition::BlockedQueue* cond_queue, Spinlock* cond_mutex) {
  TaskExecutor::_cond_queue = cond_queue;
  TaskExecutor::_cond_mutex = cond_mutex;
  co_await std::suspend_always{};
  cond_mutex->lock();
}

void TaskExecutor::exec(TaskAllocator::Handler task) {
  TaskExecutor::_running_task = std::move(task);
  TaskExecutor::_cond_mutex = nullptr;
  TaskExecutor::_cond_queue = nullptr;
  TaskExecutor::_yield_flag = false;

  auto& current = TaskExecutor::_running_task;
  auto& fn = static_cast<CoroutineBase&>(current->fn);

  ++current->execute_cnt;
  while (!TaskExecutor::_cond_mutex && !TaskExecutor::_yield_flag && !fn.done()) {
    fn.resume();
  }
  if (TaskExecutor::_cond_mutex) {
    ++current->suspend_cnt;
    TaskExecutor::_cond_queue->push(std::move(current));
    TaskExecutor::_cond_mutex->unlock();
    return;
  }
  if (TaskExecutor::_yield_flag) {
    ++current->yield_cnt;
    Partition<TaskProcessor>::get(current->ctx).scheduler(current->id).push(std::move(current));
    return;
  }
  Partition<TaskProcessor>::get(current->ctx).allocator(current->id).destroy(std::move(current));
}

}  // namespace _sched

int TaskProcessor::run_scheduled(size_t pindex, size_t batch_size) {
  int cnt = 0;
  for (; cnt < batch_size; ++cnt) {
    if (auto task = this->scheduler(pindex).pop(); task) {
      _sched::TaskExecutor::exec(std::move(*task));
    } else {
      break;
    }
  }
  if (cnt == batch_size) {
    return cnt;
  }
  for (int i = 0; i < this->_partitions.size(); ++i) {
    if (auto task = this->scheduler(pindex + i).pop(); task) {
      _sched::TaskExecutor::exec(std::move(*task));
      ++cnt;
      if (cnt == batch_size) {
        break;
      }
    }
  }
  return cnt;
}

void TaskProcessor::_drop_all_tasks() {
  for (auto& [allocator, _] : this->_partitions) {
    allocator.clear();
  }
}

}  // namespace _impl

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

void spawn(Context* ctx, Coroutine<void> fn, std::string&& name) {
  auto& dispatcher = _impl::_sched::Partition<_impl::TaskProcessor>::get(ctx);
  int id = dispatcher.allocate_id();
  auto task = dispatcher.allocator(id).create(ctx, id, std::move(fn), std::forward<std::string>(name));
  dispatcher.scheduler(id).push(std::move(task));
}

}  // namespace cgo
