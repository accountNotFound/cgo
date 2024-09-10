#pragma once

#include <atomic>
#include <functional>
#include <list>
#include <mutex>
#include <queue>
#include <unordered_map>
#include <vector>

#include "core/coroutine.h"

namespace cgo {

class Spinlock {
 public:
  void lock();

  void unlock() { _flag.store(false); }

 private:
  std::atomic<bool> _flag = false;
};

namespace _impl::_sched {

struct Task {
  int const id;
  Coroutine<void> fn;

  Task(int id, Coroutine<void> fn) : id(id), fn(std::move(fn)) { this->fn.init(); }
};

class Allocator {
 public:
  Task* create(int id, Coroutine<void> fn);

  void destroy(Task* task);

 private:
  Spinlock _mtx;
  std::unordered_map<int, std::list<Task>::iterator> _index;
  std::list<Task> _pool;
};

class Queue {
 public:
  void push(Task* task);

  Task* pop();

 private:
  Spinlock _mtx;
  std::queue<Task*> _que;
};

class Enginee {
 public:
  Enginee(size_t partition_num) : _allocators(partition_num), _q_runnables(partition_num) {}

  void create(Coroutine<void> fn);

  void destroy(Task* task) { this->_allocators[task->id % this->_allocators.size()].destroy(task); }

  bool execute(size_t index);

  void schedule(Task* task) { this->_q_runnables[task->id % this->_q_runnables.size()].push(task); }

  std::suspend_always wait(Queue& queue);

  std::suspend_always yield();

  Task* this_task() const { return Enginee::_running; }

 private:
  inline static thread_local Task* _running = nullptr;
  std::atomic<int> _gid;
  std::vector<Allocator> _allocators;
  std::vector<Queue> _q_runnables;
};

class Condition {
 public:
  Coroutine<void> wait(std::unique_lock<Spinlock>& lock);

  void notify();

 private:
  _impl::_sched::Queue _q_waiting;
};

extern std::unique_ptr<Enginee> g_enginee;

}  // namespace _impl::_sched

class Semaphore {
 public:
  Semaphore(size_t cnt) : _self(std::make_shared<Member>(cnt)) {}

  /**
   * @brief Decrease the semaphore automatically if vacant count > 0, or suspend if count == 0
   */
  Coroutine<void> aquire();

  /**
   * @brief Increase the semaphore automatically and notify another coroutine which suspend on `aquire()`
   */
  void release();

 private:
  struct Member {
    Spinlock mtx;
    _impl::_sched::Condition cond;
    size_t vacant;

    Member(size_t cnt) : vacant(cnt) {}
  };

  std::shared_ptr<Member> _self;
};

class Mutex {
 public:
  Mutex() : _sem(1) {}

  Coroutine<void> lock() { co_await _sem.aquire(); }

  void unlock() { _sem.release(); }

 private:
  Semaphore _sem;
};

inline void spawn(Coroutine<void> fn) { _impl::_sched::g_enginee->create(std::move(fn)); }

inline Coroutine<void> yield() { co_await _impl::_sched::g_enginee->yield(); }

inline int this_coroutine_id() { return _impl::_sched::g_enginee->this_task()->id; }

}  // namespace cgo
