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
  auto start() -> void { this->_func->start(); }
  auto resume() -> void { this->_func->resume(); }
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
  auto lock() -> void { this->_mutex->lock(); }
  auto unlock() -> void { this->_mutex->unlock(); }
  auto push(Coroutine&& coroutine) -> void;
  auto pop() -> std::optional<Coroutine>;
  virtual auto push_nolock(Coroutine&& coroutine) -> void = 0;
  virtual auto pop_nolock() -> std::optional<Coroutine> = 0;

 private:
  SpinLock* _mutex = nullptr;
  SpinLock _local_mutex = {};
};

class EventHandler;

class Context {
 public:
  using Callback = std::function<void()>;

 public:
  static auto current() -> Context& { return *Context::_current_context; }

 public:
  Context();
  ~Context();
  auto start(size_t executor_num) -> void;
  auto stop() -> void;
  auto spawn(std::unique_ptr<AsyncTrait>&& func, const std::string& name = "") -> void;
  auto yield(const Callback& defer = nullptr) -> std::suspend_always;
  auto wait(CoroutineSet* block_set, const Callback& defer = nullptr) -> std::suspend_always;
  auto notify(const std::vector<CoroutineSet*>& block_sets, const Callback& defer = nullptr) -> void;
  auto handler() -> EventHandler& { return *this->_event_handler; }

 public:
  template <typename T>
  auto spawn(Async<T>&& func, const std::string& name = "") -> void {
    this->spawn(std::make_unique<Async<T>>(std::move(func)), name);
  }

 private:
  auto _schedule_loop() -> void;

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
