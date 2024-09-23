#pragma once

#include <any>
#include <atomic>
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
  const int id;
  Coroutine<void> fn;
  std::vector<std::any> locals;

  Task(int id, Coroutine<void> fn) : id(id), fn(std::move(fn)) {}
};

class TaskHandler {
 public:
  TaskHandler() = default;

  TaskHandler(Task* task) : _task(task) {}

  operator bool() const { return this->_task; }

  int id() const { return this->_task->id; }

  std::vector<std::any>& locals() const { return this->_task->locals; }

  bool done() const { return this->_task->fn.done(); }

  void resume() { this->_task->fn.resume(); }

 private:
  Task* _task = nullptr;
};

class TaskAllocator {
 public:
  TaskHandler create(int id, Coroutine<void> fn);

  void destroy(TaskHandler task);

 private:
  Spinlock _mtx;
  std::unordered_map<int, std::list<Task>::iterator> _index;
  std::list<Task> _pool;
};

class TaskQueue {
 public:
  void push(TaskHandler task);

  TaskHandler pop();

 private:
  Spinlock _mtx;
  std::queue<TaskHandler> _que;
};

class TaskExecutor {
 public:
  static TaskHandler& get_running_task() { return TaskExecutor::_t_running; }

  static Coroutine<void> yield_running_task();

  static Coroutine<void> suspend_running_task(TaskQueue& q_waiting, std::unique_lock<Spinlock>& cond_lock);

  static void execute(TaskHandler task);

 private:
  inline static thread_local TaskHandler _t_running = nullptr;
  inline static thread_local TaskQueue* _suspend_q_waiting = nullptr;
  inline static thread_local std::unique_lock<Spinlock>* _suspend_cond_lock = nullptr;
  inline static thread_local bool _yield_flag = false;
};

class TaskCondition {
 public:
  Coroutine<void> wait(std::unique_lock<Spinlock>& lock);

  void notify();

 private:
  TaskQueue _q_waiting;
};

class TaskDispatcher {
 public:
  TaskDispatcher(size_t n_partition = 0) : _t_allocs(n_partition), _q_runnables(n_partition) {}

  void create(Coroutine<void> fn);

  void destroy(TaskHandler task);

  void submit(TaskHandler task);

  TaskHandler dispatch(size_t p_index);

 private:
  std::atomic<int> _tid;
  std::vector<TaskAllocator> _t_allocs;
  std::vector<TaskQueue> _q_runnables;
};

inline std::unique_ptr<TaskDispatcher> g_dispatcher = nullptr;

inline TaskDispatcher& get_dispatcher() { return *g_dispatcher; }

}  // namespace _impl::_sched

class Semaphore {
 public:
  Semaphore(size_t cnt) : _self(std::make_shared<Member>(cnt)) {}

  /**
   * @brief Decrease the semaphore automatically if vacant count > 0, or suspend if count <= 0
   */
  Coroutine<void> aquire();

  /**
   * @brief Increase the semaphore automatically and notify another coroutine which suspend on `aquire()`
   */
  void release();

 private:
  struct Member {
    Spinlock mtx;
    _impl::_sched::TaskCondition cond;
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

inline void spawn(Coroutine<void> fn) { _impl::_sched::get_dispatcher().create(std::move(fn)); }

inline Coroutine<void> yield() { return _impl::_sched::TaskExecutor::yield_running_task(); }

inline int this_coroutine_id() { return _impl::_sched::TaskExecutor::get_running_task().id(); }

inline auto& this_coroutine_locals() { return _impl::_sched::TaskExecutor::get_running_task().locals(); }

}  // namespace cgo
