#pragma once

#include <coroutine>
#include <exception>
#include <functional>

namespace cgo::_impl::_coro {

class PromiseBase {
 public:
  static void init(PromiseBase* entry);

  static void resume(PromiseBase* entry);

  static bool done(PromiseBase* entry) { return entry->_handler.done(); }

  static void call_push(PromiseBase* caller, PromiseBase* callee);

  static PromiseBase* call_pop(PromiseBase* p);

  virtual ~PromiseBase() = default;

  std::suspend_always initial_suspend() noexcept { return {}; }

  std::suspend_always final_suspend() noexcept { return {}; }

  void unhandled_exception() noexcept { this->_exception = std::current_exception(); }

 protected:
  PromiseBase* _caller = nullptr;
  PromiseBase* _callee = nullptr;
  PromiseBase* _entry = nullptr;
  PromiseBase* _current = nullptr;
  std::coroutine_handle<> _handler = nullptr;
  std::exception_ptr _exception = nullptr;
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

  std::suspend_always yield_value(T& value) {
    this->_value = value;
    return {};
  }

  std::suspend_always yield_value(T&& value) {
    this->_value = std::move(value);
    return {};
  }

 protected:
  T _value = {};
};

template <typename T>
class ValuePromiseBase<T&> : public PromiseBase {
 public:
  void return_value(T& value) { this->_value = &value; }

  std::suspend_always yield_value(T& value) {
    this->_value = &value;
    return {};
  }

 protected:
  T* _value = nullptr;
};

class CoroutineBase {
 public:
  virtual ~CoroutineBase() = default;

  virtual PromiseBase& _promise() = 0;
};

inline void init(CoroutineBase& fn) { PromiseBase::init(&fn._promise()); }

inline void resume(CoroutineBase& fn) { PromiseBase::resume(&fn._promise()); }

inline bool done(CoroutineBase& fn) { return PromiseBase::done(&fn._promise()); }

}  // namespace cgo::_impl::_coro

namespace cgo {

/**
 * @note This class has no Return Value Optimization when `co_return`. So `co_return std::move(...)` if necessary
 */
template <typename T>
class Coroutine : public _impl::_coro::CoroutineBase {
 public:
  struct promise_type
      : public std::conditional_t<std::is_void_v<T>, _impl::_coro::VoidPromiseBase, _impl::_coro::ValuePromiseBase<T>> {
    friend class Coroutine;

   public:
    promise_type() { this->_handler = std::coroutine_handle<promise_type>::from_promise(*this); }

    Coroutine get_return_object() { return Coroutine(this->_handler); }

   protected:
    using _impl::_coro::PromiseBase::call_pop;
    using _impl::_coro::PromiseBase::call_push;
  };

  Coroutine() : _type_handler(nullptr) {}

  Coroutine(const Coroutine&) = delete;

  Coroutine(Coroutine&& rhs) {
    this->_drop();
    std::swap(this->_type_handler, rhs._type_handler);
  }

  ~Coroutine() { this->_drop(); }

  bool await_ready() { return !this->_type_handler || this->_type_handler.done(); }

  void await_suspend(std::coroutine_handle<> caller) {
    using P = promise_type;
    P* caller_promise = &std::coroutine_handle<P>::from_address(caller.address()).promise();
    P* this_promise = &this->_type_handler.promise();
    _impl::_coro::PromiseBase::call_push(caller_promise, this_promise);
  }

  auto await_resume() {
    if (auto& ex = this->_type_handler.promise()._exception; ex) {
      std::rethrow_exception(ex);
    }
    if constexpr (!std::is_void_v<T>) {
      if constexpr (std::is_reference_v<T>) {
        return std::ref(*this->_type_handler.promise()._value);
      } else {
        return std::move(this->_type_handler.promise()._value);
      }
    }
  }

 protected:
  Coroutine(std::coroutine_handle<promise_type> handler) : _type_handler(handler) {}

  void _drop() {
    if (this->_type_handler) {
      this->_type_handler.destroy();
      this->_type_handler = nullptr;
    }
  }

  _impl::_coro::PromiseBase& _promise() override { return this->_type_handler.promise(); }

 private:
  std::coroutine_handle<promise_type> _type_handler;
};

}  // namespace cgo
