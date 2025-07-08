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
  _runnable_tail.link_front(task.get());
  if (_signal) {
    _signal->emit();
  }
}

auto SchedContext::Scheduler::pop() -> SchedContext::Allocator::Handler {
  std::unique_lock guard(_mtx);
  if (_runnable_head.next() == &_runnable_tail) {
    return nullptr;
  }
  auto task = _runnable_head.unlink_back();
  return Allocator::Handler(static_cast<Task*>(task));
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
  _schedule_from_this();
}

void SchedContext::Condition::_schedule_from_this() {
  if (_blocked_head.next() == &_blocked_tail) {
    return;
  }
  auto task = _blocked_head.unlink_back();
  auto ctx = task->ctx;
  auto id = task->id;
  SchedContext::at(*ctx)._scheduler(id).push(Allocator::Handler(static_cast<Task*>(task)));
}

void SchedContext::Condition::_suspend_to_this(Allocator::Handler task, Spinlock* waiting_mtx) {
  _blocked_tail.link_front(task.get());
  _mtx.unlock();
  waiting_mtx->unlock();
}

void SchedContext::Condition::_remove(Task* task) {
  std::unique_lock guard(_mtx);
  if (task->unlink_this()) {
    _schedule_from_this();
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
