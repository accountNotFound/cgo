#pragma once

#include <any>
#include <atomic>
#include <condition_variable>
#include <list>
#include <mutex>
#include <queue>
#include <unordered_map>
#include <vector>

#include "core/coroutine.h"

namespace cgo::_impl {

class Spinlock {
 public:
  void lock();

  void unlock() { _locked = false; }

 private:
  std::atomic<bool> _locked = false;
};

class BaseLazySignal {
 public:
  void emit();

  void wait(std::chrono::duration<double, std::milli> duration);

 protected:
  Spinlock _mtx;
  std::condition_variable_any _cond;
  bool _emitted = false;
  bool _waited = false;

  virtual void _emit() { _cond.notify_one(); }

  virtual void _wait(std::unique_lock<Spinlock>& guard, std::chrono::duration<double, std::milli> duration) {
    _cond.wait_for(guard, duration);
  }
};

class SchedContext {
  friend class SchedTarget;

 public:
  static auto at(Context& ctx) -> SchedContext&;

  static int this_coroutine_id() { return SchedContext::_running_task->id; }

  static auto this_coroutine_ctx() -> Context& { return *SchedContext::_running_task->ctx; }

  static auto& this_coroutine_locals() { return SchedContext::_running_task->locals; }

  SchedContext(Context& ctx, size_t n_partition)
      : _ctx(&ctx), _task_allocators(n_partition), _task_schedulers(n_partition) {}

  ~SchedContext() { clear(); }

  void clear();

  void create_scheduled(Coroutine<void>&& fn);

  void on_scheduled(size_t pindex, BaseLazySignal& signal) { _scheduler(pindex)._signal = &signal; }

  size_t run_scheduled(size_t pindex, size_t batch_size);

 private:
  class Condition;

  struct BaseTask : public BaseLinked<BaseTask> {
   public:
    Context* const ctx;
    size_t const id;

    BaseTask() : ctx(nullptr), id(-1) {};

    BaseTask(Context* ctx, size_t id) : ctx(ctx), id(id) {}
  };

  struct Task : public BaseTask {
   public:
    Coroutine<void> fn;

    bool yielded = false;
    Condition* waiting_cond = nullptr;
    Spinlock* waiting_mtx = nullptr;

    std::vector<std::any> locals;
    size_t execute_cnt = 0;
    size_t suspend_cnt = 0;
    size_t yield_cnt = 0;

    Task(Context* ctx, size_t id, Coroutine<void>&& fn) : BaseTask(ctx, id), fn(std::move(fn)) {}
  };

  class Allocator {
    friend class SchedContext;

   public:
    struct NoopDeletor {
      void operator()(void*) const {};
    };

    using Handler = std::unique_ptr<Task, NoopDeletor>;

    auto create(Context* ctx, size_t id, Coroutine<void>&& fn) -> Handler;

    void destroy(Handler);

   private:
    Spinlock _mtx;
    std::list<Task> _pool;
    std::unordered_map<size_t, std::list<Task>::iterator> _index;
  };

  class Scheduler {
    friend class SchedContext;

   public:
    Scheduler() { _runnable_head.link_back(&_runnable_tail); }

    void push(Allocator::Handler task);

    auto pop() -> Allocator::Handler;

   private:
    Spinlock _mtx;
    BaseTask _runnable_head;
    BaseTask _runnable_tail;
    BaseLazySignal* _signal = nullptr;
  };

  class Condition {
    friend class SchedContext;

   public:
    Condition() { _blocked_head.link_back(&_blocked_tail); }

    Coroutine<void> wait(std::unique_lock<Spinlock>& guard);

    void notify();

   private:
    Spinlock _mtx;
    BaseTask _blocked_head;
    BaseTask _blocked_tail;
    Context* _scheduled_ctx = nullptr;

    void _schedule_from_this();

    void _suspend_to_this(Allocator::Handler task, Spinlock* waiting_mtx);

    void _remove(Task* task);
  };

  struct Yielder {
   public:
    auto operator()() const -> std::suspend_always {
      return (SchedContext::_running_task->yielded = true, std::suspend_always{});
    }
  };

  Context* _ctx;
  std::vector<Allocator> _task_allocators;
  std::vector<Scheduler> _task_schedulers;
  inline static thread_local Allocator::Handler _running_task = nullptr;

  auto _allocator(size_t id) -> Allocator& { return _task_allocators[id % _task_allocators.size()]; }

  auto _scheduler(size_t id) -> Scheduler& { return _task_schedulers[id % _task_schedulers.size()]; }

  void _execute(Allocator::Handler task);
};

struct SchedTarget {
 public:
  struct Yielder : public SchedContext::Yielder {};

  class Condition : public SchedContext::Condition {};
};

}  // namespace cgo::_impl

namespace cgo {

class Semaphore {
 public:
  Semaphore(size_t vacant) : _vacant(vacant) {}

  Semaphore(const Semaphore&) = delete;

  Semaphore(Semaphore&&) = delete;

  Coroutine<void> aquire();

  void release();

  size_t count() { return _vacant; }

 private:
  _impl::Spinlock _mtx;
  _impl::SchedTarget::Condition _cond;
  size_t _vacant;
};

class Mutex {
 public:
  Mutex() : _sem(1) {}

  Coroutine<void> lock() { return _sem.aquire(); }

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

inline Coroutine<void> yield() { co_await _impl::SchedTarget::Yielder{}(); }

inline size_t this_coroutine_id() { return _impl::SchedContext::this_coroutine_id(); }

inline auto this_coroutine_ctx() -> Context& { return _impl::SchedContext::this_coroutine_ctx(); }

inline auto this_coroutine_locals() -> std::vector<std::any>& { return _impl::SchedContext::this_coroutine_locals(); }

inline void spawn(Context& ctx, Coroutine<void>&& fn) { _impl::SchedContext::at(ctx).create_scheduled(std::move(fn)); }

}  // namespace cgo
