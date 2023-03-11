#include "context.h"

#include <chrono>
#include <queue>

// #define USE_DEBUG
#include "util/log.h"

namespace cgo::impl {

Coroutine::Coroutine(std::unique_ptr<AsyncTrait>&& func, const std::string& name)
    : _func(std::move(func)), _name(name != "" ? name : "co_" + std::to_string(size_t(this))) {}

auto CoroutineSet::push(Coroutine&& coroutine) -> void {
  std::unique_lock guard(*this->_mutex);
  this->push_nolock(std::move(coroutine));
}

auto CoroutineSet::pop() -> std::optional<Coroutine> {
  std::unique_lock guard(*this->_mutex);
  return this->pop_nolock();
}

class RunnableSet : public CoroutineSet {
 public:
  RunnableSet(SpinLock& mutex) : CoroutineSet(mutex) {}
  auto push_nolock(Coroutine&& coroutine) -> void { _queue.push(std::move(coroutine)); }
  auto pop_nolock() -> std::optional<Coroutine> {
    if (_queue.size() == 0) {
      return std::nullopt;
    }
    auto res = std::move(_queue.front());
    _queue.pop();
    return res;
  }

 private:
  std::queue<Coroutine> _queue;
};

thread_local Context* Context::_current_context = nullptr;
thread_local Coroutine* Context::_running_coroutine = nullptr;

Context::Context() { this->_runnable_set = std::make_unique<RunnableSet>(this->_mutex); }

auto Context::initialize(size_t executor_num) -> void {
  this->_finish = false;
  for (int i = 0; i < executor_num; i++) {
    this->_executors.emplace_back(std::thread(std::bind(&Context::_schedule_loop, this)));
  }
}

auto Context::finalize() -> void {
  this->_finish = true;
  for (auto& exec : this->_executors) {
    exec.join();
  }
  this->_executors.clear();
}

auto Context::start(std::unique_ptr<AsyncTrait>&& func, const std::string& name) -> void {
  Coroutine coro(std::move(func), name);
  coro.start();
  this->_runnable_set->push(std::move(coro));
}

auto Context::yield(const Callback& defer) -> std::suspend_always {
  auto tid = std::this_thread::get_id();
  DEBUG("[TH-{%u}]:: yield coroutine{%s}", tid, Context::_running_coroutine->name().data());
  Context::_running_coroutine->set_status(Coroutine::Status::Yield);
  this->_runnable_set->push(std::move(*Context::_running_coroutine));
  Context::_running_coroutine = nullptr;
  if (defer) {
    defer();
  }
  return std::suspend_always{};
}

auto Context::wait(CoroutineSet* block_set, const Callback& defer) -> std::suspend_always {
  auto tid = std::this_thread::get_id();
  DEBUG("[TH-{%u}]:: suspend coroutine{%s}", tid, Context::_running_coroutine->name().data());
  Context::_running_coroutine->set_status(Coroutine::Status::Blocked);
  block_set->push(std::move(*Context::_running_coroutine));
  Context::_running_coroutine = nullptr;
  if (defer) {
    defer();
  }
  return std::suspend_always{};
}

auto Context::notify(const std::vector<CoroutineSet*>& block_sets, const Callback& defer) -> void {
  for (auto& set : block_sets) {
    auto coro_wrapper = set->pop();
    if (coro_wrapper.has_value()) {
      this->_runnable_set->push(std::move(coro_wrapper.value()));
    }
  }
  if (defer) {
    defer();
  }
}

auto Context::_schedule_loop() -> void {
  auto tid = std::this_thread::get_id();
  Context::_current_context = this;
  while (!this->_finish) {
    auto coro_wrapper = this->_runnable_set->pop();
    if (!coro_wrapper.has_value()) {
      DEBUG("[TH-{%u}]:: sleep", tid);
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      continue;
    }
    auto& coro = coro_wrapper.value();
    coro.set_status(Coroutine::Status::Runnable);
    Context::_running_coroutine = &coro;
    const char* name = coro.name().data();
    DEBUG("[TH-{%u}]:: start execute coroutine{%s}", tid, name);
    while (!coro.done() && coro.status() == Coroutine::Status::Runnable) {
      coro.resume();
      if (!Context::_running_coroutine) {
        break;
      }
    }
    DEBUG("[TH-{%u}]:: end execute coroutine{%s}", tid, name);
  }
}

auto Context::_event_loop() -> void {
  // TODO
}

}  // namespace cgo::impl
