#include "core/schedule.h"

#include <random>
#include <thread>

namespace cgo {

void Spinlock::lock() {
  bool expected = false;
  while (!_flag.compare_exchange_weak(expected, true)) {
    expected = false;
  }
}

namespace _impl::_sched {

std::unique_ptr<Enginee> g_enginee = nullptr;

Task* Allocator::create(int id, Coroutine<void> fn) {
  std::unique_lock guard(this->_mtx);
  this->_index[id] = this->_pool.emplace(this->_pool.end(), id, std::move(fn));
  return &*this->_index[id];
}

void Allocator::destroy(Task* task) {
  std::unique_lock guard(this->_mtx);
  int id = task->id;
  this->_pool.erase(this->_index[id]);
  this->_index.erase(id);
}

void Queue::push(Task* task) {
  std::unique_lock guard(this->_mtx);
  this->_que.push(task);
}

Task* Queue::pop() {
  std::unique_lock guard(this->_mtx);
  if (this->_que.empty()) {
    return nullptr;
  }
  Task* task = this->_que.front();
  this->_que.pop();
  return task;
}

void Enginee::create(Coroutine<void> fn) {
  int id = this->_gid.fetch_add(1);
  auto task = this->_allocators[id % this->_allocators.size()].create(id, std::move(fn));
  this->schedule(task);
}

bool Enginee::execute(size_t index) {
  Task* task = nullptr;
  for (int i = 0; i < this->_q_runnables.size() && !task; i++) {
    task = this->_q_runnables[(index + i) % this->_q_runnables.size()].pop();
  }
  if (!task) {
    return false;
  }

  Enginee::_running = task;
  while (Enginee::_running && !Enginee::_running->fn.done()) {
    Enginee::_running->fn.resume();
  }
  if (Enginee::_running) {
    this->destroy(Enginee::_running);
  }
  return true;
}

std::suspend_always Enginee::wait(Queue& queue) {
  queue.push(Enginee::_running);
  Enginee::_running = nullptr;
  return {};
}

std::suspend_always Enginee::yield() {
  int slot = Enginee::_running->id % this->_q_runnables.size();
  this->_q_runnables[slot].push(Enginee::_running);
  Enginee::_running = nullptr;
  return {};
}

Coroutine<void> Condition::wait(std::unique_lock<Spinlock>& lock) {
  lock.unlock();
  co_await g_enginee->wait(this->_q_waiting);
  lock.lock();
}

void Condition::notify() {
  if (auto next = this->_q_waiting.pop(); next) {
    g_enginee->schedule(next);
  }
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
  std::unique_lock guard(_self->mtx);
  _self->vacant++;
  _self->cond.notify();
}

}  // namespace cgo
