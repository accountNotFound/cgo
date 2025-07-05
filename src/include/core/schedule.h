#pragma once

#include <any>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <list>
#include <mutex>
#include <optional>
#include <queue>
#include <unordered_map>
#include <vector>

#include "core/coroutine.h"

namespace cgo {

namespace _impl {

class Spinlock {
 public:
  void lock();

  void unlock();

 private:
  std::atomic<bool> _lock_flag = false;
};

template <typename T>
class LockfreeQueue {
 public:
  void push(T&& x) {
    std::unique_lock guard(this->_mtx);
    this->_que.push(std::forward<T>(x));
  }

  auto pop() -> std::optional<T> {
    std::unique_lock guard(this->_mtx);
    if (this->_que.empty()) {
      return std::nullopt;
    }
    auto res = std::move(this->_que.front());
    this->_que.pop();
    return res;
  }

 protected:
  Spinlock _mtx;
  std::queue<T> _que;
};

class LazySignalBase {
 public:
  virtual void emit();

  virtual void wait(std::chrono::duration<double, std::milli> duration);

 protected:
  Spinlock _mtx;
  std::condition_variable_any _cond;
  bool _signal_flag = false;
  bool _wait_flag = false;
};

namespace _sched {

struct Task {
 public:
  Context* const ctx;
  int const id;
  Coroutine<void> fn;
  std::vector<std::any> locals;

  // fields for debugging
  std::chrono::steady_clock::time_point create_at;
  std::string name = "";
  int execute_cnt = 0;
  int yield_cnt = 0;
  int suspend_cnt = 0;

  Task(Context* ctx, int id, Coroutine<void> fn, std::string&& name = "")
      : ctx(ctx), id(id), fn(std::move(fn)), name(std::forward<std::string>(name)) {
    this->create_at = std::chrono::steady_clock::now();
  }

  Task(const Task&) = delete;

  Task(Task&&) = delete;
};

class TaskAllocator {
 public:
  struct NoopDeletor {
    void operator()(void*) const {}
  };

  using Handler = std::unique_ptr<Task, NoopDeletor>;

  auto create(Context* ctx, int id, Coroutine<void> fn, std::string&& name = "") -> Handler;

  void destroy(Handler task);

  void clear();

 private:
  Spinlock _mtx;
  std::list<Task> _pool;
  std::unordered_map<int, std::list<Task>::iterator> _index;
};

class TaskScheduler : public LockfreeQueue<TaskAllocator::Handler> {
 public:
  void push(TaskAllocator::Handler task);

  void on_scheduled(LazySignalBase& signal) { this->_signal = &signal; }

 private:
  LazySignalBase* _signal = nullptr;
};

class TaskCondition {
 public:
  using BlockedQueue = LockfreeQueue<TaskAllocator::Handler>;

  Coroutine<void> wait(std::unique_lock<Spinlock>& lock);

  void notify();

 private:
  BlockedQueue _blocked_tasks;
};

class TaskExecutor {
 public:
  static void exec(TaskAllocator::Handler task);

  static Coroutine<void> yield();

  static Coroutine<void> wait(TaskCondition::BlockedQueue* cond_queue, Spinlock* cond_mutex);

  static auto get() -> Task& { return *TaskExecutor::_running_task; }

 private:
  inline static thread_local TaskAllocator::Handler _running_task = nullptr;
  inline static thread_local TaskCondition::BlockedQueue* _cond_queue = nullptr;
  inline static thread_local Spinlock* _cond_mutex = nullptr;
  inline static thread_local bool _yield_flag = false;
};

template <typename Derieved>
class Partition {
 public:
  static auto get(Context* ctx) -> Partition& { return Derieved::get(ctx); }

  Partition(size_t n_partition) : _partitions(n_partition) {}

  int allocate_id() { return this->_tid.fetch_add(1); }

  auto allocator(int i) -> TaskAllocator& { return this->_partitions[this->_loc(i)].allocator; }

  auto scheduler(int i) -> TaskScheduler& { return this->_partitions[this->_loc(i)].scheduler; }

 protected:
  struct Impl {
    TaskAllocator allocator;
    TaskScheduler scheduler;
  };

  std::atomic<int> _tid = 0;
  std::vector<Impl> _partitions;

  int _loc(int i) { return i % this->_partitions.size(); }
};

}  // namespace _sched

class TaskProcessor : public _sched::Partition<TaskProcessor> {
 public:
  static auto get(Context* ctx) -> TaskProcessor&;

  TaskProcessor(size_t n_partition) : _sched::Partition<TaskProcessor>(n_partition) {}

  ~TaskProcessor() { this->_drop_all_tasks(); }

  void on_scheduled(size_t pindex, LazySignalBase& signal) { this->scheduler(pindex).on_scheduled(signal); }

  int run_scheduled(size_t pindex, size_t batch_size);

 private:
  using _sched::Partition<TaskProcessor>::get;
  using _sched::Partition<TaskProcessor>::allocate_id;
  using _sched::Partition<TaskProcessor>::allocator;
  using _sched::Partition<TaskProcessor>::scheduler;

  void _drop_all_tasks();
};

}  // namespace _impl

class Semaphore {
 public:
  Semaphore(int vacant) : _vacant(vacant) {}

  Semaphore(const Semaphore&) = delete;

  Coroutine<void> aquire();

  void release();

  int count() { return this->_vacant; }

 private:
  _impl::Spinlock _mtx;
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

inline Coroutine<void> yield() { return _impl::_sched::TaskExecutor::yield(); }

inline int this_coroutine_id() { return _impl::_sched::TaskExecutor::get().id; }

inline auto this_coroutine_ctx() -> Context* { return _impl::_sched::TaskExecutor::get().ctx; }

inline auto& this_coroutine_locals() { return _impl::_sched::TaskExecutor::get().locals; }

void spawn(Context* ctx, Coroutine<void> fn, std::string&& name = "");

}  // namespace cgo
