#pragma once

#include <coroutine>
#include <exception>
#include <memory>
#include <stack>

namespace cgo::_impl::_coro {

class CoroutineBase;

void init(CoroutineBase& fn);

bool done(CoroutineBase& fn);

void resume(CoroutineBase& fn);

class PromiseBase {
  friend void init(CoroutineBase& fn);
  friend bool done(CoroutineBase& fn);
  friend void resume(CoroutineBase& fn);

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
  T _value;
};

class CoroutineBase {
  friend void init(CoroutineBase& fn);
  friend bool done(CoroutineBase& fn);
  friend void resume(CoroutineBase& fn);

 protected:
  virtual ~CoroutineBase() = default;

  virtual std::coroutine_handle<> _handler() = 0;

  virtual PromiseBase& _promise() = 0;
};

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
    Coroutine get_return_object() { return Coroutine(std::coroutine_handle<promise_type>::from_promise(*this)); }
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
    P& promise = std::coroutine_handle<P>::from_address(caller.address()).promise();
    (this->_type_handler.promise()._stack = promise._stack)->push(this->_type_handler);
  }

  T await_resume() {
    if (auto& ex = this->_type_handler.promise()._exception; ex) {
      std::rethrow_exception(ex);
    }
    if constexpr (!std::is_void_v<T>) {
      return std::move(this->_type_handler.promise()._value);
    }
  }

 private:
  Coroutine(std::coroutine_handle<promise_type> handler) : _type_handler(handler) {}

  void _drop() {
    if (this->_type_handler) {
      this->_type_handler.destroy();
      this->_type_handler = nullptr;
    }
  }

  std::coroutine_handle<> _handler() override { return this->_type_handler; }

  _impl::_coro::PromiseBase& _promise() override { return this->_type_handler.promise(); }

 private:
  std::coroutine_handle<promise_type> _type_handler;
};

}  // namespace cgo
