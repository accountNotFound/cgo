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
  void submit_sender(const ChanEvent& ev) { this->_senders.push(ev); }

  void submit_receiver(const ChanEvent& ev) { this->_receivers.push(ev); }

  bool to(void* dst, MoveFn move_fn);

  bool from(void* src, MoveFn move_fn);

  bool to(ChanEvent& dst, MoveFn move_fn);

  bool from(ChanEvent& src, MoveFn move_fn);

 private:
  std::queue<ChanEvent> _senders, _receivers;
};

template <typename T>
class Channel {
  friend class Select;

 public:
  static void move(void* src, void* dst) { *static_cast<T*>(dst) = std::move(*static_cast<T*>(src)); }

  Channel(size_t capacity) : _capacity(capacity) {}

  void submit_sender(ChanEvent src) {
    std::unique_lock guard(this->_mtx);
    if (this->_buffer.empty()) {
      if (this->_matcher.from(src, Channel::move)) {
        return;
      }
    }
    // buffer not empty, so no waiting receiver
    if (this->_buffer.size() < this->_capacity) {
      if (T x; src.to(&x, Channel::move) == ChanEvent::MoveStatus::Ok) {
        this->_buffer.push(std::move(x));
        return;
      }
    }
    this->_matcher.submit_sender(src);
  }

  void submit_receiver(ChanEvent dst) {
    std::unique_lock guard(this->_mtx);
    if (this->_buffer.empty()) {
      if (this->_matcher.to(dst, Channel::move)) {
        return;
      }
    } else if (dst.from(&this->_buffer.front(), Channel::move) == ChanEvent::MoveStatus::Ok) {
      this->_buffer.pop();
      if (T x; this->_matcher.to(&x, Channel::move)) {
        this->_buffer.push(std::move(x));
      }
      return;
    }
    this->_matcher.submit_receiver(dst);
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
 * @brief A copyable reference to real channel object
 */
template <typename T>
class Channel {
  friend class Select;

 public:
  Channel(size_t capacity = 0) : _impl(std::make_shared<_impl::_chan::Channel<T>>(capacity)) {}

  Coroutine<void> operator<<(const T& x) {
    T x_copy = x;
    Semaphore sem(0);
    this->_impl->submit_sender(_impl::_chan::ChanEvent(&x_copy, &sem));
    co_await sem.aquire();
  }

  Coroutine<void> operator<<(T&& x) {
    Semaphore sem(0);
    this->_impl->submit_sender(_impl::_chan::ChanEvent(&x, &sem));
    co_await sem.aquire();
  }

  Coroutine<void> operator>>(T& x) {
    Semaphore sem(0);
    this->_impl->submit_receiver(_impl::_chan::ChanEvent(&x, &sem));
    co_await sem.aquire();
  }

 private:
  std::shared_ptr<_impl::_chan::Channel<T>> _impl;
};

/**
 * @brief An one-shot select object
 * @note Touch select object after `Select::operator()` return is undefined behaviour
 */
class Select {
 public:
  template <typename T>
  class Case {
   public:
    Case(Select& select, _impl::_chan::Channel<T>& chan, int key, std::shared_ptr<_impl::_chan::SelectReducer>& reducer)
        : _select(&select), _chan(&chan), _key(key), _reducer(reducer) {}

    void operator<<(const T& x) {
      this->_select->_invokers.emplace_back([x, chan = _chan, key = _key, reducer = _reducer]() {
        chan->submit_sender(_impl::_chan::ChanEvent(&x, key, reducer));
      });
      this->_select = nullptr;
    }

    void operator<<(T&& x) {
      this->_select->_invokers.emplace_back([x = std::move(x), chan = _chan, key = _key, reducer = _reducer]() {
        chan->submit_sender(_impl::_chan::ChanEvent(const_cast<T*>(&x), key, reducer));
      });
      this->_select = nullptr;
    }

    void operator>>(T& x) {
      this->_select->_invokers.emplace_back([&x, chan = _chan, key = _key, reducer = _reducer]() {
        chan->submit_receiver(_impl::_chan::ChanEvent(&x, key, reducer));
      });
      this->_select = nullptr;
    }

   private:
    Select* _select;
    _impl::_chan::Channel<T>* _chan;
    int _key;
    std::shared_ptr<_impl::_chan::SelectReducer> _reducer;
    ;
  };

  Select() : _reducer(std::make_shared<_impl::_chan::SelectReducer>()) {}

  Select(const Select&) = delete;

  /**
   * @param key: The unique key specifing certain channel event. Key should be >= 0
   */
  template <typename T>
  Case<T> on(int key, Channel<T>& chan) {
    return Case<T>(*this, *chan._impl, key, this->_reducer);
  }

  /**
   * @return Unique key set in `Select::on()`, or -1 if `with_default=true` and no channel event comes to activate
   */
  Coroutine<int> operator()(bool with_default);

 private:
  std::shared_ptr<_impl::_chan::SelectReducer> _reducer;
  std::vector<std::function<void()>> _invokers;
};

}  // namespace cgo
