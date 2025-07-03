#pragma once

#include <coroutine>
#include <exception>
#include <functional>

namespace cgo {

namespace _impl {

class CoroutineBase;

namespace _coro {

class PromiseBase {
 public:
  static void call_stack_create(PromiseBase* promise);

  static void call_stack_destroy(PromiseBase* promise);

  static void call_stack_execute(PromiseBase* promise);

  static void call_stack_push(PromiseBase* promise, PromiseBase* callee);

  virtual ~PromiseBase() = default;

  auto initial_suspend() noexcept -> std::suspend_always { return {}; }

  auto final_suspend() noexcept -> std::suspend_always { return {}; }

  void unhandled_exception() noexcept { this->_exception = std::current_exception(); }

 private:
  static auto _call_stack_pop(PromiseBase* promise) -> PromiseBase*;

 public:
  CoroutineBase* coro = nullptr;

 protected:
  std::exception_ptr _exception = nullptr;

 private:
  PromiseBase* _caller = nullptr;
  PromiseBase* _callee = nullptr;
  PromiseBase* _entry = nullptr;
  PromiseBase* _current = nullptr;
};

class VoidPromiseBase : public PromiseBase {
 public:
  void return_void() const {}
};

template <typename T>
class ValuePromiseBase : public PromiseBase {
 public:
  void return_value(T& value) { this->_value = value; }

  void return_value(T&& value) { this->_value = std::move(value); }

  auto yield_value(T& value) -> std::suspend_always { this->_value = value, return {}; }

  auto yield_value(T&& value) -> std::suspend_always { this->_value = std::move(value), return {}; }

 protected:
  T _value = {};
};

template <typename T>
class ValuePromiseBase<T&> : public PromiseBase {
 public:
  void return_value(T& value) { this->_value = &value; }

  auto yield_value(T& value) -> std::suspend_always { this->_value = &value, return {}; }

 protected:
  T* _value = nullptr;
};

template <typename T>
class ValuePromiseBase<T&&> : public ValuePromiseBase<std::remove_reference_t<T>> {};

}  // namespace _coro

class CoroutineBase {
 public:
  template <typename T>
  CoroutineBase(std::coroutine_handle<T> handler) : handler(handler), _promise(&handler.promise()) {}

  CoroutineBase(const CoroutineBase&) = delete;

  CoroutineBase(CoroutineBase&& rhs);

  virtual ~CoroutineBase() { this->_destroy_coro_frame(); }

  void init() { this->_promise->coro = this, _coro::PromiseBase::call_stack_create(this->_promise); }

  void resume() { _coro::PromiseBase::call_stack_execute(this->_promise); }

  bool done() { return this->handler.done(); }

  void destroy() { _coro::PromiseBase::call_stack_destroy(this->_promise); }

 private:
  void _destroy_coro_frame();

 public:
  std::coroutine_handle<> handler;

 protected:
  _coro::PromiseBase* _promise;
};

}  // namespace _impl

/**
 * @note This class has no Return Value Optimization when `co_return`. So `co_return std::move(...)` if necessary
 *
 *      Don't move `Coroutine` object after `co_await` or the process will exit
 */
template <typename T>
class Coroutine : public _impl::CoroutineBase {
 public:
  using RetType = T;

  struct promise_type
      : public std::conditional_t<std::is_void_v<T>, _impl::_coro::VoidPromiseBase, _impl::_coro::ValuePromiseBase<T>> {
    friend class Coroutine;

   public:
    Coroutine get_return_object() { return Coroutine(std::coroutine_handle<promise_type>::from_promise(*this)); }

   private:
    using _impl::_coro::PromiseBase::call_stack_create;
    using _impl::_coro::PromiseBase::call_stack_destroy;
    using _impl::_coro::PromiseBase::call_stack_execute;
    using _impl::_coro::PromiseBase::call_stack_push;

   private:
    using _impl::_coro::PromiseBase::coro;
  };

 public:
  Coroutine() : Coroutine(nullptr) {}

  bool await_ready() { return !this->handler || this->handler.done(); }

  void await_suspend(std::coroutine_handle<> caller) {
    using P = promise_type;
    P* caller_promise = &std::coroutine_handle<P>::from_address(caller.address()).promise();
    this->_promise->coro = this;
    _impl::_coro::PromiseBase::call_stack_push(caller_promise, this->_promise);
  }

  auto await_resume() {
    if (auto& ex = this->get_promise()._exception; ex) {
      std::rethrow_exception(ex);
    }
    if constexpr (!std::is_void_v<T>) {
      if constexpr (std::is_lvalue_reference_v<T>) {
        return std::ref(*this->get_promise()._value);
      } else {
        return std::move(this->get_promise()._value);
      }
    }
  }

 private:
  Coroutine(std::coroutine_handle<promise_type> handler) : _impl::CoroutineBase(handler) {}

  promise_type& get_promise() { return dynamic_cast<promise_type&>(*this->_promise); }

 private:
  using _impl::CoroutineBase::handler;
};

}  // namespace cgo
