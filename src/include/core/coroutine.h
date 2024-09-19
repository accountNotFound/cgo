#pragma once

#include <coroutine>
#include <exception>
#include <memory>
#include <stack>

namespace cgo::_impl::_coro {

struct SwitchTo {
 public:
  SwitchTo(std::coroutine_handle<> target) : _target(target) {}

  bool await_ready() const noexcept { return false; }

  std::coroutine_handle<> await_suspend(std::coroutine_handle<>) const noexcept { return this->_target; }

  void await_resume() const noexcept {}

 private:
  std::coroutine_handle<> _target;
};

class PromiseBase {
 public:
  std::suspend_always initial_suspend() noexcept { return {}; }

  SwitchTo final_suspend() noexcept {
    this->_stack->pop();
    return SwitchTo(this->_stack->empty() ? std::noop_coroutine() : this->_stack->top());
  }

  void unhandled_exception() noexcept { this->_exception = std::current_exception(); }

 protected:
  std::shared_ptr<std::stack<std::coroutine_handle<>>> _stack = nullptr;
  std::exception_ptr _exception = nullptr;
};

class VoidPromiseBase : public PromiseBase {
 public:
  void return_void() const {}
};

template <typename T>
class ValuePromiseBase : public PromiseBase {
 public:
  void return_value(T&& value) { this->_value = std::move(value); }

 protected:
  T _value;
};

}  // namespace cgo::_impl::_coro

namespace cgo {

template <typename T>
class Coroutine {
 public:
  struct promise_type
      : public std::conditional_t<std::is_void_v<T>, _impl::_coro::VoidPromiseBase, _impl::_coro::ValuePromiseBase<T>> {
    friend class Coroutine;

   public:
    Coroutine get_return_object() { return Coroutine(std::coroutine_handle<promise_type>::from_promise(*this)); }
  };

  Coroutine() : _handler(nullptr) {}

  Coroutine(const Coroutine&) = delete;

  Coroutine(Coroutine&& rhs) {
    this->_drop();
    std::swap(this->_handler, rhs._handler);
  }

  ~Coroutine() { this->_drop(); }

  void init() {
    (this->_promise()._stack = std::make_shared<std::stack<std::coroutine_handle<>>>())->push(this->_handler);
  }

  void resume() {
    this->_promise()._stack->top().resume();
    if (auto& ex = this->_promise()._exception; ex) {
      std::rethrow_exception(ex);
    }
  }

  bool done() { return this->_handler.done(); }

  bool await_ready() { return this->done(); }

  std::coroutine_handle<> await_suspend(std::coroutine_handle<> caller) {
    promise_type& promise = std::coroutine_handle<promise_type>::from_address(caller.address()).promise();
    (this->_promise()._stack = promise._stack)->push(this->_handler);
    return this->_handler;
  }

  T await_resume() {
    if (auto& ex = this->_promise()._exception; ex) {
      std::rethrow_exception(ex);
    }
    if constexpr (!std::is_same_v<T, void>) {
      return std::move(this->_promise()._value);
    }
  }

 private:
  std::coroutine_handle<promise_type> _handler;

  Coroutine(std::coroutine_handle<promise_type> handler) : _handler(handler) {}

  promise_type& _promise() { return this->_handler.promise(); }

  void _drop() {
    if (this->_handler) {
      this->_handler.destroy();
    }
  }
};

}  // namespace cgo
