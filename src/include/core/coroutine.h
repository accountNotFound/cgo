#pragma once

#include <coroutine>
#include <exception>
#include <functional>

namespace cgo::_impl {

class BaseFrame {
  friend class FrameOperator;

 public:
  BaseFrame(const BaseFrame&) = delete;

  BaseFrame(BaseFrame&&);

  virtual ~BaseFrame() { _destroy(); }

 protected:
  class BasePromise {
    friend class BaseFrame;
    friend class FrameOperator;

   public:
    virtual ~BasePromise() = default;

    auto initial_suspend() noexcept -> std::suspend_always { return {}; }

    auto final_suspend() noexcept -> std::suspend_always { return {}; }

    void unhandled_exception() noexcept { this->_error = std::current_exception(); }

   protected:
    BaseFrame* _onwer = nullptr;
    std::exception_ptr _error = nullptr;

    void _call_stack_push(BasePromise* callee);

    auto _call_stack_pop() -> BasePromise*;

   private:
    BasePromise* _caller = nullptr;
    BasePromise* _callee = nullptr;
    BasePromise* _entry = nullptr;
    BasePromise* _current = nullptr;
  };

  class VoidPromise : public BasePromise {
   public:
    void return_void() const {}
  };

  template <typename T>
  class ValuePromise : public BasePromise {
   public:
    void return_value(T& value) { this->_value = value; }

    void return_value(T&& value) { this->_value = std::move(value); }

    auto yield_value(T& value) -> std::suspend_always { return (this->_value = value, std::suspend_always{}); }

    auto yield_value(T&& value) -> std::suspend_always {
      return (this->_value = std::move(value), std::suspend_always{});
    }

   protected:
    T _value = {};
  };

  template <typename T>
  class ValuePromise<T&> : public BasePromise {
   public:
    void return_value(T& value) { this->_value = &value; }

    auto yield_value(T& value) -> std::suspend_always { return (this->_value = &value, std::suspend_always{}); }

   protected:
    T* _value = nullptr;
  };

  template <typename T>
  class ValuePromise<T&&> : public ValuePromise<std::remove_reference_t<T>> {};

  std::coroutine_handle<> _handler = nullptr;
  BasePromise* _promise = nullptr;

  template <typename T>
  BaseFrame(std::coroutine_handle<T> h) : _handler(h), _promise(&h.promise()) {}

  void _destroy();
};

class FrameOperator {
 public:
  static void init(BaseFrame& entry) { _call_stack_create(entry); }

  static void resume(BaseFrame& entry) { _call_stack_execute(entry); }

  static void destroy(BaseFrame& entry) { _call_stack_destroy(entry); }

  static bool done(BaseFrame& entry) { return entry._handler.done(); }

 private:
  static void _call_stack_create(BaseFrame& entry);

  static void _call_stack_destroy(BaseFrame& entry);

  static void _call_stack_execute(BaseFrame& entry);
};

}  // namespace cgo::_impl

namespace cgo {

class Context;

template <typename T>
class Coroutine : public _impl::BaseFrame {
 public:
  using RetType = T;

  struct promise_type
      : public std::conditional_t<std::is_void_v<T>, _impl::BaseFrame::VoidPromise, _impl::BaseFrame::ValuePromise<T>> {
    friend class Coroutine;

   public:
    Coroutine get_return_object() { return Coroutine(std::coroutine_handle<promise_type>::from_promise(*this)); }
  };

  void* address() const { return _handler.address(); }

  bool await_ready() { return _handler.done(); }

  void await_suspend(std::coroutine_handle<> caller) {
    promise_type& p = std::coroutine_handle<promise_type>::from_address(caller.address()).promise();
    _promise()._onwer = this;
    p._call_stack_push(&_promise());
  }

  auto await_resume() {
    if (auto& ex = _promise()._error; ex) {
      std::rethrow_exception(ex);
    }
    if constexpr (!std::is_void_v<T>) {
      if constexpr (std::is_lvalue_reference_v<T>) {
        return std::ref(*_promise()._value);
      } else {
        return std::move(_promise()._value);
      }
    }
  }

 private:
  Coroutine(std::coroutine_handle<promise_type> h) : _impl::BaseFrame(h) {}

  auto _promise() -> promise_type& { return *static_cast<promise_type*>(_impl::BaseFrame::_promise); }
};

}  // namespace cgo
