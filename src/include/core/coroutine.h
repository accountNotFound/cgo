#pragma once

#include <coroutine>
#include <exception>
#include <functional>

namespace cgo::_impl {

template <typename T>
class BaseLinked {
 public:
  auto front() -> T* const
    requires std::is_base_of_v<BaseLinked<T>, T>
  {
    return static_cast<T*>(_prev);
  }

  auto back() -> T* const
    requires std::is_base_of_v<BaseLinked<T>, T>
  {
    return static_cast<T*>(_next);
  }

  void link_back(BaseLinked<T>* bx) {
    auto old_next = _next;
    this->_next = bx;
    bx->_next = old_next;
    bx->_prev = this;
    if (old_next) {
      old_next->_prev = bx;
    }
  }

  void link_front(BaseLinked<T>* bx) {
    auto old_prev = _prev;
    this->_prev = bx;
    bx->_prev = old_prev;
    bx->_next = this;
    if (old_prev) {
      old_prev->_next = bx;
    }
  }

  bool unlink_this() {
    auto old_prev = _prev;
    auto old_next = _next;
    if (old_prev) {
      old_prev->_next = old_next;
    }
    if (old_next) {
      old_next->_prev = old_prev;
    }
    bool ok = _prev || _next;
    _prev = _next = nullptr;
    return ok;
  }

  auto unlink_back() -> T*
    requires std::is_base_of_v<BaseLinked<T>, T>
  {
    if (!_next) {
      return nullptr;
    }
    auto x = _next;
    auto new_next = x->_next;
    this->_next = new_next;
    if (new_next) {
      new_next->_prev = this;
    }
    x->_prev = x->_next = nullptr;
    return static_cast<T*>(x);
  }

  auto unlink_front() -> T*
    requires std::is_base_of_v<BaseLinked<T>, T>
  {
    if (!_prev) {
      return nullptr;
    }
    auto x = _prev;
    auto new_prev = x->_prev;
    this->_prev = new_prev;
    if (new_prev) {
      new_prev->_next = this;
    }
    x->_prev = x->_next = nullptr;
    return static_cast<T*>(x);
  }

 private:
  BaseLinked<T>* _prev = nullptr;
  BaseLinked<T>* _next = nullptr;
};

class BaseFrame {
  friend class FrameOperator;

 public:
  BaseFrame(const BaseFrame&) = delete;

  BaseFrame(BaseFrame&&);

  virtual ~BaseFrame() { _destroy(); }

  BaseFrame& operator=(BaseFrame&&) = delete;

 protected:
  class BasePromise : private BaseLinked<BasePromise> {
    friend class BaseLinked<BasePromise>;
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

  BaseFrame() = default;

  template <typename T>
  BaseFrame(std::coroutine_handle<T> h) : _handler(h), _promise(&h.promise()) {
    _promise->_onwer = this;
  }

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
  using Returned = T;

  struct promise_type
      : public std::conditional_t<std::is_void_v<T>, _impl::BaseFrame::VoidPromise, _impl::BaseFrame::ValuePromise<T>> {
    friend class Coroutine;

   public:
    Coroutine get_return_object() { return Coroutine(std::coroutine_handle<promise_type>::from_promise(*this)); }
  };

  Coroutine(nullptr_t) : _impl::BaseFrame() {}

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
