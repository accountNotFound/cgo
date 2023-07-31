#pragma once

#include <any>
#include <coroutine>
#include <exception>
#include <memory>
#include <stack>

namespace cgo::impl {

using HandlerTrait = std::coroutine_handle<>;

template <typename T>
concept NoVoid = requires(T) { !std::is_same_v<T, void>; };

class AsyncTrait;

class PromiseTrait {
 public:
  auto initial_suspend() const -> std::suspend_always { return std::suspend_always{}; }
  auto final_suspend() const noexcept -> std::suspend_always { return std::suspend_always{}; }
  auto unhandled_exception() noexcept -> void { this->_error = std::current_exception(); }

  auto set_wrapper(AsyncTrait& wrapper) -> void { this->_wrapper = &wrapper; }
  auto wrapper() -> AsyncTrait& { return *this->_wrapper; }
  void set_exception(const std::exception_ptr& e) { this->_error = e; }
  auto exception() -> std::exception_ptr { return this->_error; }

 public:
  template <NoVoid T>
  auto get() -> T {
    if constexpr (!std::is_same_v<T, std::any>) {
      return std::any_cast<T>(std::move(this->_value));
    } else {
      return std::move(this->_value);
    }
  }

 protected:
  auto _any() const -> const std::any& { return this->_value; }
  auto _yield_any(std::any&& value) -> std::suspend_always {
    this->_value = std::move(value);
    return std::suspend_always{};
  }

 protected:
  AsyncTrait* _wrapper = nullptr;
  std::exception_ptr _error = nullptr;
  std::any _value = nullptr;
};

template <typename T>
class Promise : public PromiseTrait {
 public:
  auto yield_value(T&& value) -> void { this->_yield_any(std::move(value)); }
  auto return_value(T&& value) -> void { this->_yield_any(std::move(value)); }
};

template <>
class Promise<void> : public PromiseTrait {
 public:
  auto return_void() const -> void {}
};

class AsyncTrait {
 public:
  AsyncTrait(const AsyncTrait&) = delete;
  AsyncTrait(AsyncTrait&&);
  ~AsyncTrait();
  auto start() -> void;
  auto resume() -> void;
  auto done() const -> bool;
  auto call(AsyncTrait& callee) -> void;

 protected:
  template <typename P>
    requires std::is_base_of_v<PromiseTrait, P>
  AsyncTrait(std::coroutine_handle<P> handler) : _handler(handler) {
    this->_promise = &handler.promise();
    this->_promise->set_wrapper(*this);
  }

 protected:
  HandlerTrait _handler;
  PromiseTrait* _promise;
  std::shared_ptr<std::stack<AsyncTrait*>> _stack;
};

template <typename T>
class Async : public AsyncTrait {
 public:
  struct promise_type : public Promise<T> {
    auto get_return_object() -> Async { return Async(std::coroutine_handle<promise_type>::from_promise(*this)); }
  };

 public:
  auto await_ready() const -> bool { return this->done(); }

 public:
  template <typename P>
    requires std::is_base_of_v<PromiseTrait, P>
  auto await_suspend(std::coroutine_handle<P> caller) -> void {
    caller.promise().wrapper().call(*this);
  }

 public:
  auto await_resume() -> T {
    if (this->_promise->exception()) {
      std::rethrow_exception(this->_promise->exception());
    }
    if constexpr (!std::is_same_v<T, void>) {
      return this->_promise->get<T>();
    }
  }

 private:
  Async(std::coroutine_handle<promise_type> handler) : AsyncTrait(handler) {}
};

}  // namespace cgo::impl
