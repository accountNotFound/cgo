#pragma once

#include <coroutine>
#include <exception>
#include <memory>
#include <stack>

namespace cgo::_impl::_coro {

class PromiseBase {
 public:
  std::suspend_always initial_suspend() noexcept { return {}; }

  std::suspend_always final_suspend() noexcept { return {}; }

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

  T get_value() { return std::move(this->_value); }

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
    static promise_type& from(std::coroutine_handle<> h) {
      return std::coroutine_handle<promise_type>::from_address(h.address()).promise();
    }

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
    std::stack<std::coroutine_handle<>>& stk = *this->_promise()._stack;
    std::coroutine_handle<> current;
    do {
      current = stk.top();
      current.resume();
      if (auto& ex = promise_type::from(current)._exception; ex) {
        stk.pop();
        if (stk.empty()) {
          std::rethrow_exception(ex);
        }
        promise_type::from(stk.top())._exception = ex;
        continue;
      }
      if (current.done()) {
        stk.pop();
        continue;
      }
    } while (!stk.empty() && stk.top() != current);
  }

  bool done() { return this->_handler.done(); }

  bool await_ready() { return this->done(); }

  void await_suspend(std::coroutine_handle<> caller) {
    (this->_promise()._stack = promise_type::from(caller)._stack)->push(this->_handler);
  }

  T await_resume() {
    if (auto& ex = this->_promise()._exception; ex) {
      std::rethrow_exception(ex);
    }
    if constexpr (!std::is_same_v<T, void>) {
      return std::move(this->_promise().get_value());
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
