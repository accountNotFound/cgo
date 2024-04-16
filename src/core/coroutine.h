#pragma once

#include <coroutine>
#include <exception>
#include <memory>
#include <stack>

namespace cgo::_impl {

class CoroutinePromiseBase {
 public:
  std::suspend_always initial_suspend() const { return {}; }
  std::suspend_always final_suspend() const noexcept { return {}; }
  void unhandled_exception() noexcept { this->error = std::current_exception(); }

 public:
  std::exception_ptr error = nullptr;
  std::coroutine_handle<> co_handler = nullptr;
  std::shared_ptr<std::stack<CoroutinePromiseBase*>> co_frames = nullptr;
};

template <typename T>
class CoroutinePromise;

template <typename T>
  requires std::integral_constant<bool, !std::is_same_v<T, void>>::value
class CoroutinePromise<T> : public CoroutinePromiseBase {
 public:
  void return_value(T&& value) { this->value = std::move(value); }

 public:
  T value;
};

template <>
class CoroutinePromise<void> : public CoroutinePromiseBase {
 public:
  void return_void() {}
};

class CoroutineBase {
 public:
  CoroutineBase(const CoroutineBase&) = delete;
  CoroutineBase(CoroutineBase&& rhs);
  virtual ~CoroutineBase();
  void start();
  void resume();
  bool done();
  CoroutinePromiseBase& promise() const { return *this->_promise; }

 protected:
  CoroutineBase(CoroutinePromiseBase* p) : _promise(p) {}

 protected:
  CoroutinePromiseBase* _promise = nullptr;
};

}  // namespace cgo::_impl

namespace cgo {

template <typename T>
class Coroutine : public _impl::CoroutineBase {
 public:
  using type = T;

 public:
  class promise_type : public _impl::CoroutinePromise<T> {
   public:
    Coroutine get_return_object() {
      this->co_handler = std::coroutine_handle<decltype(*this)>::from_promise(*this);
      return Coroutine(this);
    }
  };

 public:
  Coroutine(nullptr_t) : CoroutineBase(nullptr) {}

  bool await_ready() { return this->done(); }

  template <typename PromiseType>
    requires std::is_base_of_v<_impl::CoroutinePromiseBase, PromiseType>
  void await_suspend(std::coroutine_handle<PromiseType> caller) {
    this->_promise->co_frames = caller.promise().co_frames;
    this->_promise->co_frames->push(this->_promise);
  }

  T await_resume() {
    if (this->_promise->error) {
      std::rethrow_exception(this->_promise->error);
    }
    if constexpr (!std::is_same_v<T, void>) {
      return std::move(static_cast<promise_type*>(this->_promise)->value);
    }
  }

 private:
  Coroutine(_impl::CoroutinePromiseBase* p) : CoroutineBase(p) {}
};

}  // namespace cgo
