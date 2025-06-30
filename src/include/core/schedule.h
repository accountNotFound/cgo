#pragma once

#include <any>
#include <atomic>
#include <condition_variable>
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

  void unlock() { this->_flag.store(false); }

 private:
  std::atomic<bool> _flag = false;
};

namespace _impl {

class SignalBase {
 public:
  virtual void emit();

  virtual void wait(const std::chrono::duration<double, std::milli>& duration);

 protected:
  Spinlock _mtx;
  std::condition_variable_any _cond;
  bool _signal_flag = false;
  bool _wait_flag = false;
};

}  // namespace _impl

namespace _impl::_sched {

struct Task {
  const int id;
  Coroutine<void> fn;
  const std::string name;

  std::vector<std::any> locals = {};
  std::chrono::steady_clock::time_point create_at = {};
  int execute_cnt = 0;
  int yield_cnt = 0;
  int suspend_cnt = 0;

  Task(int id, Coroutine<void> fn, const std::string& name) : id(id), fn(std::move(fn)), name(name) {}
};

class TaskHandler {
 public:
  TaskHandler() = default;

  TaskHandler(Task* task) : _task(task) {}

  Task& get() { return *this->_task; }

  operator bool() const { return this->_task; }

  Task* operator->() const { return this->_task; }

 private:
  Task* _task = nullptr;
};

class TaskAllocator {
 public:
  TaskHandler create(int id, Coroutine<void> fn, const std::string& name = "");

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

  void regist(_impl::SignalBase& signal) { this->_signal = &signal; }

 private:
  Spinlock _mtx;
  std::queue<TaskHandler> _que;
  _impl::SignalBase* _signal = nullptr;
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
  inline static thread_local Spinlock* _suspend_cond_lock = nullptr;
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
  TaskDispatcher(size_t n_partition) : _tid(0), _t_allocs(n_partition), _q_runnables(n_partition) {}

  void create(Coroutine<void> fn, const std::string& name = "");

  void destroy(TaskHandler task);

  void submit(TaskHandler task);

  TaskHandler dispatch(size_t p_index);

  void regist(size_t p_index, SignalBase& signal) { this->_q_runnables[p_index].regist(signal); }

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
  Semaphore(int vacant) : _vacant(vacant) {}

  Semaphore(const Semaphore&) = delete;

  Coroutine<void> aquire();

  void release();

  int count() { return this->_vacant; }

 private:
  Spinlock _mtx;
  _impl::_sched::TaskCondition _cond;
  int _vacant;
};

class Mutex {
 public:
  Mutex() : _sem(1) {}

  Coroutine<void> lock() { return this->_sem.aquire(); }

  void unlock() { _sem.release(); }

 private:
  Semaphore _sem;
};

class DeferGuard {
 public:
  DeferGuard(std::function<void()>&& fn) : _defer(std::forward<std::function<void()>>(fn)) {}

  DeferGuard(const DeferGuard&) = delete;

  DeferGuard(DeferGuard&&);

  ~DeferGuard();

  void drop() { this->_defer = nullptr; }

 private:
  std::function<void()> _defer = nullptr;
};

[[nodiscard]] inline DeferGuard defer(std::function<void()>&& fn) {
  return DeferGuard(std::forward<std::function<void()>>(fn));
}

inline void spawn(Coroutine<void> fn, const std::string& name = "") {
  _impl::_sched::get_dispatcher().create(std::move(fn), name);
}

inline Coroutine<void> yield() { return _impl::_sched::TaskExecutor::yield_running_task(); }

inline int this_coroutine_id() { return _impl::_sched::TaskExecutor::get_running_task()->id; }

inline auto& this_coroutine_locals() { return _impl::_sched::TaskExecutor::get_running_task()->locals; }

}  // namespace cgo
