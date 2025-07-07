#pragma once

#include <functional>

#include "core/schedule.h"

namespace cgo::_impl::_chan {

using MoveFn = void (*)(void*, void*);

struct SelectReducer {
 public:
  Semaphore sem;
  Spinlock mtx;
  int key;
  bool done;

  SelectReducer() : sem(0), key(-1), done(false) {}

  void commit(int key);
};

class ChanEvent {
 public:
  enum class MoveStatus { Ok, SrcInvalid, DstInvalid };

  ChanEvent(void* data, Semaphore* sem) : _data(data), _chan_sem(sem), _select_key(-1), _select_reduce(nullptr) {}

  ChanEvent(void* data, int key, std::shared_ptr<SelectReducer> reducer)
      : _data(data), _chan_sem(nullptr), _select_key(key), _select_reduce(reducer) {}

  MoveStatus to(void* dst, MoveFn move_fn);

  MoveStatus from(void* src, MoveFn move_fn);

  MoveStatus to(ChanEvent& dst, MoveFn move_fn);

  MoveStatus from(ChanEvent& src, MoveFn move_fn) { return src.to(*this, move_fn); }

 private:
  void* _data;
  Semaphore* _chan_sem;
  int _select_key;
  std::shared_ptr<SelectReducer> _select_reduce;
};

class ChanEventMatcher {
 public:
  void submit_sender(ChanEvent ev) { this->_senders.push(std::move(ev)); }

  void submit_receiver(ChanEvent ev) { this->_receivers.push(std::move(ev)); }

  bool to(void* dst, MoveFn move_fn);

  bool from(void* src, MoveFn move_fn);

  bool to(ChanEvent& dst, MoveFn move_fn);

  bool from(ChanEvent& src, MoveFn move_fn);

 private:
  std::queue<ChanEvent> _senders, _receivers;
};

template <typename T>
class Channel {
 public:
  static void move(void* src, void* dst) { *static_cast<T*>(dst) = std::move(*static_cast<T*>(src)); }

  Channel(size_t capacity) : _capacity(capacity) {}

  bool recv_from(ChanEvent src, bool enable_async = true) {
    std::unique_lock guard(this->_mtx);
    if (this->_buffer.empty()) {
      if (this->_matcher.from(src, Channel::move)) {
        return true;
      }
    }
    // buffer not empty, so no waiting receiver
    if (this->_buffer.size() < this->_capacity) {
      if (T x; src.to(&x, Channel::move) == ChanEvent::MoveStatus::Ok) {
        this->_buffer.push(std::move(x));
        return true;
      }
    }
    if (enable_async) {
      this->_matcher.submit_sender(src);
    }
    return false;
  }

  bool send_to(ChanEvent dst, bool enable_async = true) {
    std::unique_lock guard(this->_mtx);
    if (this->_buffer.empty()) {
      if (this->_matcher.to(dst, Channel::move)) {
        return true;
      }
    } else if (dst.from(&this->_buffer.front(), Channel::move) == ChanEvent::MoveStatus::Ok) {
      this->_buffer.pop();
      if (T x; this->_matcher.to(&x, Channel::move)) {
        this->_buffer.push(std::move(x));
      }
      return true;
    }
    if (enable_async) {
      this->_matcher.submit_receiver(dst);
    }
    return false;
  }

 private:
  const size_t _capacity;
  Spinlock _mtx;
  std::queue<T> _buffer;
  ChanEventMatcher _matcher;
};

}  // namespace cgo::_impl::_chan

namespace cgo {

/**
 * @brief Representation for void type
 */
struct Nil {};

struct Dropout {};

/**
 * @brief A copyable reference to real channel object
 */
template <typename T>
class Channel {
  friend class Select;

 private:
  class Nowait {
   public:
    Nowait(_impl::_chan::Channel<T>& chan) : _chan(&chan) {}

    bool operator<<(T& x) {
      Semaphore sem(0);
      return this->_chan->recv_from(_impl::_chan::ChanEvent(&x, &sem), /*enable_async=*/false);
    }

    bool operator<<(T&& x) {
      Semaphore sem(0);
      return this->_chan->recv_from(_impl::_chan::ChanEvent(&x, &sem), /*enable_async=*/false);
    }

   private:
    _impl::_chan::Channel<T>* _chan;
  };

 public:
  Channel(size_t capacity = 0) : _impl(std::make_shared<_impl::_chan::Channel<T>>(capacity)) {}

  Coroutine<void> operator<<(T& x) {
    T x_copy = x;
    Semaphore sem(0);
    this->_impl->recv_from(_impl::_chan::ChanEvent(&x_copy, &sem));
    co_await sem.aquire();
  }

  Coroutine<void> operator<<(T&& x) {
    Semaphore sem(0);
    this->_impl->recv_from(_impl::_chan::ChanEvent(&x, &sem));
    co_await sem.aquire();
  }

  Coroutine<void> operator>>(T& x) {
    Semaphore sem(0);
    this->_impl->send_to(_impl::_chan::ChanEvent(&x, &sem));
    co_await sem.aquire();
  }

  Coroutine<void> operator>>(Dropout) {
    Semaphore sem(0);
    T x;
    this->_impl->send_to(_impl::_chan::ChanEvent(&x, &sem));
    co_await sem.aquire();
  }

  Nowait nowait() { return Nowait(*_impl); }

 private:
  std::shared_ptr<_impl::_chan::Channel<T>> _impl;
};

/**
 * @brief An one-shot select object. Bind some channels with unique key by called `Select::on()`,
 *
 *        then `co_await Select::operator()()` to wait and get a key, which represents corresponding
 *
 *        channel envet happened
 *
 * @note Touch select object after `Select::operator()()` return is undefined behaviour
 */
class Select {
 private:
  template <typename T>
  class Case {
    friend class Select;

   public:
    void operator<<(T& x) {
      this->_select->_invokers.emplace_back([x, chan = _chan, key = _key, reducer = _reducer]() {
        chan->recv_from(_impl::_chan::ChanEvent(&x, key, reducer));
      });
      this->_select = nullptr;
    }

    void operator<<(T&& x) {
      this->_select->_invokers.emplace_back([x = std::move(x), chan = _chan, key = _key, reducer = _reducer]() {
        chan->recv_from(_impl::_chan::ChanEvent(const_cast<T*>(&x), key, reducer));
      });
      this->_select = nullptr;
    }

    void operator>>(T& x) {
      this->_select->_invokers.emplace_back([&x, chan = _chan, key = _key, reducer = _reducer]() {
        chan->send_to(_impl::_chan::ChanEvent(&x, key, reducer));
      });
      this->_select = nullptr;
    }

    void operator>>(Dropout) {
      this->_select->_invokers.emplace_back([chan = _chan, key = _key, reducer = _reducer]() {
        T x;
        chan->send_to(_impl::_chan::ChanEvent(&x, key, reducer));
      });
      this->_select = nullptr;
    }

   private:
    Select* _select;
    _impl::_chan::Channel<T>* _chan;
    int _key;
    std::shared_ptr<_impl::_chan::SelectReducer> _reducer;

    Case(Select& select, _impl::_chan::Channel<T>& chan, int key, std::shared_ptr<_impl::_chan::SelectReducer>& reducer)
        : _select(&select), _chan(&chan), _key(key), _reducer(reducer) {}
  };

 public:
  Select() : _reducer(std::make_shared<_impl::_chan::SelectReducer>()) {}

  Select(const Select&) = delete;

  /**
   * @param key: The unique key specifing certain channel event. Key should be >= 0
   */
  template <typename T>
  Case<T> on(int key, Channel<T>& chan) {
    return Case<T>(*this, *chan._impl, key, this->_reducer);
  }

  template <typename T>
  Case<T> on(int key, Channel<T>&& chan) {
    this->_storages.push_back(chan._impl);
    return Case<T>(*this, *chan._impl, key, this->_reducer);
  }

  void on_default(int key);

  Coroutine<int> operator()();

 private:
  std::shared_ptr<_impl::_chan::SelectReducer> _reducer;
  std::vector<std::function<void()>> _invokers;
  std::vector<std::shared_ptr<void>> _storages;
  bool _enable_default = false;
};

/**
 * @brief Spawn `fn` and collect returned value, send it into returned channel
 * @note Returned channel will contain `cgo::Nil{}` if `T=void`
 */
template <typename T, typename V = std::conditional_t<std::is_void_v<T>, Nil, T>>
Channel<V> collect(Context& ctx, Coroutine<T> fn) {
  Channel<V> chan(0);
  cgo::spawn(ctx, [](Coroutine<T> fn, Channel<V> chan) -> cgo::Coroutine<void> {
    if constexpr (std::is_void_v<T>) {
      co_await fn;
      co_await (chan << Nil{});
    } else {
      V res = co_await fn;
      co_await (chan << std::move(res));
    }
  }(std::move(fn), chan));
  return chan;
}

}  // namespace cgo
