#include "context.h"

#include <chrono>
#include <queue>

#include "event.h"

// #define USE_DEBUG
#include "util/log.h"

namespace cgo::impl {

const size_t SchedulerBaseWaitMilliSec = 5;
const size_t ScheduleMaxWaitMilliSec = 50;
const size_t EventHandlerFdCapacity = 1024;

Coroutine::Coroutine(std::unique_ptr<AsyncTrait>&& func, const std::string& name)
    : _func(std::move(func)), _name(name != "" ? name : "co_" + std::to_string(size_t(this))) {}

auto CoroutineSet::push(Coroutine&& coroutine) {
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
  void push_nolock(Coroutine&& coroutine) { _queue.push(std::move(coroutine)); }
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

Context::Context() {
  if (Context::_current_context != nullptr) {
    throw "try to create new context in thread where another context already exists !!!";
  }
  Context::_current_context = this;
  this->_runnable_set = std::make_unique<RunnableSet>(this->_mutex);
  this->_event_handler = std::make_unique<EventHandler>(EventHandlerFdCapacity);
}

Context::~Context() {
  this->stop();
  Context::_current_context = nullptr;
}

void Context::start(size_t executor_num) {
  this->_finish = false;
  for (int i = 0; i < executor_num; i++) {
    this->_executors.emplace_back(std::thread(std::bind(&Context::_schedule_loop, this)));
  }
}

void Context::stop() {
  this->_finish = true;
  for (auto& exec : this->_executors) {
    exec.join();
  }
  this->_executors.clear();
}

void Context::spawn(std::unique_ptr<AsyncTrait>&& func, const std::string& name) {
  Coroutine coro(std::move(func), name);
  coro.start();
  this->_runnable_set->push(std::move(coro));
}

auto Context::yield(const std::function<void()>& defer) -> std::suspend_always {
  auto tid = std::this_thread::get_id();
  DEBUG("[TH-{%u}]: yield coroutine{%s}", tid, Context::_running_coroutine->name().data());
  this->_runnable_set->push(std::move(*Context::_running_coroutine));
  Context::_running_coroutine = nullptr;
  if (defer) {
    defer();
  }
  return std::suspend_always{};
}

auto Context::wait(CoroutineSet* block_set, const std::function<void()>& defer) -> std::suspend_always {
  auto tid = std::this_thread::get_id();
  DEBUG("[TH-{%u}]: suspend coroutine{%s}", tid, Context::_running_coroutine->name().data());
  block_set->push(std::move(*Context::_running_coroutine));
  Context::_running_coroutine = nullptr;
  if (defer) {
    defer();
  }
  return std::suspend_always{};
}

void Context::notify(const std::vector<CoroutineSet*>& block_sets, const std::function<void()>& defer) {
  auto tid = std::this_thread::get_id();
  for (auto& set : block_sets) {
    auto coro_wrapper = set->pop();
    if (coro_wrapper.has_value()) {
      DEBUG("[TH-{%u}]: notify coroutine{%s}", tid, coro_wrapper.value().name().data());
      this->_runnable_set->push(std::move(coro_wrapper.value()));
    }
  }
  if (defer) {
    defer();
  }
}

void Context::_schedule_loop() {
  auto tid = std::this_thread::get_id();
  Context::_current_context = this;
  size_t wait_millisec = SchedulerBaseWaitMilliSec;
  while (!this->_finish) {
    DEBUG("[TH-{%u}]: continue", tid);
    auto coro_wrapper = this->_runnable_set->pop();
    if (!coro_wrapper.has_value()) {
      DEBUG("[TH-{%u}]: sleep", tid);
      std::this_thread::sleep_for(std::chrono::milliseconds(wait_millisec));
      wait_millisec += SchedulerBaseWaitMilliSec;
      wait_millisec = std::min(wait_millisec, ScheduleMaxWaitMilliSec);
      continue;
    }
    wait_millisec = SchedulerBaseWaitMilliSec;
    Context::_running_coroutine = &coro_wrapper.value();
    auto name = Context::_running_coroutine->name().data();
    DEBUG("[TH-{%u}]: execute start: coroutine{%s}", tid, name);
    while (Context::_running_coroutine && !Context::_running_coroutine->done()) {
      // DEBUG("[TH-{%u}]: resume start: coroutine{%s}", tid, name);
      Context::_running_coroutine->resume();
      // `current_coroutine` may be set to nullptr in yield() and wait() in resume() frame, in which case this
      // coroutine is moved to corresponding set. Break this resume loop and get next runnable coroutine
      // DEBUG("[TH-{%u}]: resume end: coroutine{%s}", tid, name);
    }
    DEBUG("[TH-{%u}]: execute end: coroutine{%s}", tid, name);
  }
}

}  // namespace cgo::impl
