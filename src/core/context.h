#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

#include "async.h"
#include "util/spin_lock.h"

namespace cgo::impl {

class Coroutine {
 public:
  Coroutine(std::unique_ptr<AsyncTrait>&& func, const std::string& name = "");
  void start() { this->_func->start(); }
  void resume() { this->_func->resume(); }
  auto done() const -> bool { return this->_func->done(); }
  auto name() const -> const std::string& { return this->_name; }

 private:
  std::unique_ptr<AsyncTrait> _func;
  const std::string _name;
};

class CoroutineSet {
 public:
  CoroutineSet() { this->_mutex = &this->_local_mutex; }
  CoroutineSet(SpinLock& mutex) : _mutex(&mutex) {}
  auto lock() { this->_mutex->lock(); }
  auto unlock() { this->_mutex->unlock(); }
  auto push(Coroutine&& coroutine);
  auto pop() -> std::optional<Coroutine>;
  virtual void push_nolock(Coroutine&& coroutine) = 0;
  virtual auto pop_nolock() -> std::optional<Coroutine> = 0;

 private:
  SpinLock* _mutex = nullptr;
  SpinLock _local_mutex = {};
};

class EventHandler;

class Context {
 public:
  static auto current() -> Context& { return *Context::_current_context; }

 public:
  Context();
  ~Context();
  void start(size_t executor_num);
  void stop();
  void spawn(std::unique_ptr<AsyncTrait>&& func, const std::string& name = "");
  auto yield(const std::function<void()>& defer = nullptr) -> std::suspend_always;
  auto wait(CoroutineSet* block_set, const std::function<void()>& defer = nullptr) -> std::suspend_always;
  void notify(const std::vector<CoroutineSet*>& block_sets, const std::function<void()>& defer = nullptr);
  auto handler() -> EventHandler& { return *this->_event_handler; }

 public:
  template <typename T>
  void spawn(Async<T>&& func, const std::string& name = "") {
    this->spawn(std::make_unique<Async<T>>(std::move(func)), name);
  }

 private:
  void _schedule_loop();

 private:
  static thread_local Context* _current_context;
  static thread_local Coroutine* _running_coroutine;

 private:
  SpinLock _mutex;
  std::unique_ptr<CoroutineSet> _runnable_set;
  std::vector<std::thread> _executors;
  std::unique_ptr<EventHandler> _event_handler;
  bool _finish = false;
};

}  // namespace cgo::impl
