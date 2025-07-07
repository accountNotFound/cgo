#include "core/schedule.h"

namespace cgo::_impl {

void Spinlock::lock() {
  bool expected = false;
  while (!_locked.compare_exchange_weak(expected, true)) {
    expected = false;
  }
}

void BaseLazySignal::emit() {
  if (_waited && !_emitted) {
    std::unique_lock guard(_mtx);
    if (_waited && !_emitted) {
      _emit();
      _emitted = true;
    }
  }
}

void BaseLazySignal::wait(std::chrono::duration<double, std::milli> duration) {
  if (!_emitted) {
    std::unique_lock guard(_mtx);
    if (!_emitted) {
      _emitted = false;
      _waited = true;
      _wait(guard, duration);
      _waited = false;
    }
  }
}

void SchedContext::clear() {
  for (auto& allocator : _task_allocators) {
    for (auto& task : allocator._pool) {
      if (task.waiting_cond) {
        task.waiting_cond->_remove(&task);
      }
      FrameOperator::destroy(task.fn);
    }
  }
  _task_schedulers.clear();
  _task_allocators.clear();
}

void SchedContext::create_scheduled(Coroutine<void>&& fn) {
  size_t id = reinterpret_cast<size_t>(fn.address());
  auto task = _allocator(id).create(_ctx, id, std::move(fn));
  _scheduler(id).push(std::move(task));
}

size_t SchedContext::run_scheduled(size_t pindex, size_t batch_size) {
  size_t cnt = 0;
  for (; cnt < batch_size; ++cnt) {
    if (auto task = _scheduler(pindex).pop(); task) {
      _execute(std::move(task));
    } else {
      break;
    }
  }
  if (cnt == batch_size) {
    return cnt;
  }
  for (size_t i = 0; i < _task_schedulers.size(); ++i) {
    if (auto task = _scheduler(pindex + i).pop(); task) {
      _execute(std::move(task));
      ++cnt;
      if (cnt == batch_size) {
        break;
      }
    }
  }
  return cnt;
}

auto SchedContext::Allocator::create(Context* ctx, size_t id, Coroutine<void>&& fn) -> Handler {
  Task* task = nullptr;
  {
    std::unique_lock guard(_mtx);
    _index[id] = _pool.emplace(_pool.end(), ctx, id, std::move(fn));
    task = &*_index[id];
  }
  FrameOperator::init(task->fn);
  return Handler(task);
}

void SchedContext::Allocator::destroy(SchedContext::Allocator::Handler task) {
  size_t id = task->id;
  {
    std::unique_lock guard(_mtx);
    _pool.erase(_index.at(id));
    _index.erase(id);
  }
}

void SchedContext::Scheduler::push(SchedContext::Allocator::Handler task) {
  std::unique_lock guard(_mtx);
  _runnable_tasks.push(std::move(task));
  if (_signal) {
    _signal->emit();
  }
}

auto SchedContext::Scheduler::pop() -> SchedContext::Allocator::Handler {
  std::unique_lock guard(_mtx);
  if (_runnable_tasks.empty()) {
    return nullptr;
  }
  auto task = std::move(_runnable_tasks.front());
  _runnable_tasks.pop();
  return std::move(task);
}

Coroutine<void> SchedContext::Condition::wait(std::unique_lock<Spinlock>& guard) {
  _mtx.lock();  // unlock in caller
  auto& current = *SchedContext::_running_task;
  current.waiting_cond = this;
  auto mtx = current.waiting_mtx = guard.release();
  co_await std::suspend_always{};  // do schedule in caller by call _suspend_to_this()
  guard = std::unique_lock(*mtx);  // unlocked in caller
}

void SchedContext::Condition::notify() {
  std::unique_lock guard(_mtx);
  _notify_inlock();
}

void SchedContext::Condition::_schedule_from_this(std::list<Allocator::Handler>& blocked_queue) {
  if (blocked_queue.empty()) {
    return;
  }
  auto task = std::move(blocked_queue.front());
  blocked_queue.pop_front();
  auto ctx = task->ctx;
  auto id = task->id;
  _index.erase(id);
  SchedContext::at(*ctx)._scheduler(id).push(std::move(task));
}

void SchedContext::Condition::_suspend_to_this(Allocator::Handler task, Spinlock* waiting_mtx) {
  auto& que = _blocked_tasks[task->ctx];
  auto id = task->id;
  _index[id] = que.emplace(que.end(), std::move(task));
  _mtx.unlock();
  waiting_mtx->unlock();
}

void SchedContext::Condition::_remove(Task* task) {
  std::unique_lock guard(_mtx);
  auto ctx = task->ctx;
  auto id = task->id;
  if (_index.contains(id)) {
    _blocked_tasks[ctx].erase(_index[id]);
    _index.erase(id);
    _notify_inlock();
  }
}

void SchedContext::Condition::_notify_inlock() {
  // notify this context
  if (SchedContext::_running_task) {
    if (auto& que = _blocked_tasks[SchedContext::_running_task->ctx]; !que.empty()) {
      _schedule_from_this(que);
      return;
    }
  }

  // choose a context randomly
  int no_empty_cnt = 0;
  for (auto& [_, que] : _blocked_tasks) {
    if (!que.empty()) {
      ++no_empty_cnt;
    }
  }
  int i = 0;
  for (auto& [_, que] : _blocked_tasks) {
    if (que.empty()) {
      continue;
    }
    ++i;
    int r = std::rand() % no_empty_cnt;
    if (r % no_empty_cnt == 0 || i == no_empty_cnt) {
      _schedule_from_this(que);
    }
  }
}

void SchedContext::_execute(SchedContext::Allocator::Handler task) {
  auto& current = SchedContext::_running_task = std::move(task);
  current->yielded = false;
  current->waiting_cond = nullptr;
  current->waiting_mtx = nullptr;

  ++current->execute_cnt;
  while (!current->yielded && !current->waiting_cond && !FrameOperator::done(current->fn)) {
    FrameOperator::resume(current->fn);
  }
  if (FrameOperator::done(current->fn)) {
    _allocator(current->id).destroy(std::move(current));
  } else if (current->yielded) {
    ++current->yield_cnt;
    _scheduler(current->id).push(std::move(current));
  } else {
    ++current->suspend_cnt;
    auto mtx = current->waiting_mtx;
    current->waiting_cond->_suspend_to_this(std::move(current), mtx);
  }
}

}  // namespace cgo::_impl

namespace cgo {

Coroutine<void> Semaphore::aquire() {
  std::unique_lock guard(_mtx);
  while (true) {
    if (_vacant > 0) {
      _vacant--;
      break;
    }
    co_await _cond.wait(guard);
  }
}

void Semaphore::release() {
  {
    std::unique_lock guard(_mtx);
    _vacant++;
    _cond.notify();
  }
}

DeferGuard::DeferGuard(DeferGuard&& rhs) {
  drop();
  std::swap(_defer, rhs._defer);
}

DeferGuard::~DeferGuard() {
  if (_defer) {
    _defer();
    _defer = nullptr;
  }
}

}  // namespace cgo
