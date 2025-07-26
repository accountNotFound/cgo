#pragma once

#include <queue>
#include <variant>

#include "core/schedule.h"

namespace cgo {

class Select;

namespace _impl {

class BaseChannel;

class BaseMsg : public BaseLinked<BaseMsg> {
  friend class BaseChannel;

 public:
  enum class TransferStatus { Ok = 0, InvalidSrc, InvalidDst };

  struct Simplex {
    void* data;
    Semaphore* signal;

    void commit();
  };

  struct Multiplex {
    void* data;
    Select* select;
    int case_key;

    void commit();
  };

  BaseMsg() = default;

  BaseMsg(std::variant<Simplex, Multiplex> msg) : _msg(msg) {}

  virtual ~BaseMsg() = default;

  auto send_to(void* dst) -> TransferStatus;

  auto send_to(BaseMsg* dst) -> TransferStatus;

  auto recv_from(void* src) -> TransferStatus;

  auto recv_from(BaseMsg* src) -> TransferStatus { return src->send_to(this); }

  void drop();

 protected:
  std::variant<Simplex, Multiplex> _msg;
  BaseChannel* _chan = nullptr;

  virtual void _move(void* src, void* dst) {};
};

template <typename T>
class TypeMsg : public BaseMsg {
 public:
  TypeMsg() = default;

  TypeMsg(std::variant<Simplex, Multiplex> msg) : BaseMsg(msg) {}

 private:
  void _move(void* src, void* dst) override {
    if (src && dst) {
      *static_cast<T*>(dst) = std::move(*static_cast<T*>(src));
    }
  }
};

class BaseChannel {
  friend class BaseMsg;

 public:
  enum class TransferStatus { Ok = 0, InvalidSrc, InvalidDst, InvalidOneshot, Inprocess };

  BaseChannel();

  virtual ~BaseChannel() = default;

  auto send_to(BaseMsg* dst, bool oneshot = false) -> TransferStatus;

  auto recv_from(BaseMsg* src, bool oneshot = false) -> TransferStatus;

 protected:
  Spinlock _mtx;
  BaseMsg _sender_head, _sender_tail;
  BaseMsg _recver_head, _recver_tail;

  virtual bool _buffer_send_to(BaseMsg* dst) = 0;

  virtual bool _buffer_recv_from(BaseMsg* src) = 0;
};

template <typename T>
class TypeChannel : public BaseChannel {
 public:
  TypeChannel(size_t capacity = 0) : _capacity(capacity) {}

 private:
  const size_t _capacity;
  std::queue<T> _buffer;

  bool _buffer_send_to(BaseMsg* dst) override {
    if (_buffer.empty()) {
      return false;
    }
    if (dst->recv_from(&_buffer.front()) == BaseMsg::TransferStatus::Ok) {
      _buffer.pop();
      return true;
    }
    return false;
  }

  bool _buffer_recv_from(BaseMsg* src) override {
    if (_buffer.size() == _capacity) {
      return false;
    }
    if (T x; src->send_to(&x) == BaseMsg::TransferStatus::Ok) {
      _buffer.push(std::move(x));
      return true;
    }
    return false;
  }
};

}  // namespace _impl

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

 public:
  struct Nowait {
    friend class Channel;

   public:
    bool operator<<(T& x) const {
      _impl::BaseMsg::Simplex simplex{&x, nullptr};
      _impl::TypeMsg<T> msg(simplex);
      if (_chan->recv_from(&msg, /*oneshot=*/true) == _impl::BaseChannel::TransferStatus::Ok) {
        return true;
      }
      return false;
    }

    bool operator<<(T&& x) const {
      T data = std::move(x);
      return *this << data;
    }

   private:
    _impl::TypeChannel<T>* _chan;

    Nowait(_impl::TypeChannel<T>* chan) : _chan(chan) {}
  };

  Channel(size_t capacity = 0) : _chan(std::make_shared<_impl::TypeChannel<T>>(capacity)) {}

  Coroutine<void> operator<<(T& x) {
    Semaphore signal(0);
    _impl::BaseMsg::Simplex simplex{&x, &signal};
    _impl::TypeMsg<T> msg(simplex);
    auto guard = defer([&msg]() { msg.drop(); });
    _chan->recv_from(&msg);
    co_await signal.aquire();
  }

  Coroutine<void> operator<<(T&& x) {
    T data = std::move(x);
    co_await (*this << ((T&)(data)));
  }

  Coroutine<void> operator>>(T& x) {
    Semaphore signal(0);
    _impl::BaseMsg::Simplex simplex{&x, &signal};
    _impl::TypeMsg<T> msg(simplex);
    auto guard = defer([&msg]() { msg.drop(); });
    _chan->send_to(&msg);
    co_await signal.aquire();
  }

  Coroutine<void> operator>>(Dropout) {
    Semaphore signal(0);
    _impl::BaseMsg::Simplex simplex{nullptr, &signal};
    _impl::TypeMsg<T> msg(simplex);
    auto guard = defer([&msg]() { msg.drop(); });
    _chan->send_to(&msg);
    co_await signal.aquire();
  }

  Nowait nowait() const { return Nowait(_chan.get()); }

 private:
  std::shared_ptr<_impl::TypeChannel<T>> _chan;
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
  friend class _impl::BaseMsg;

 public:
  template <typename T>
  class Case {
    friend class Select;

   public:
    void operator<<(T& x) {
      auto listener = [&x, msg = _impl::TypeMsg<T>(), chan = _chan, select = _select, key = _key]() mutable {
        _impl::BaseMsg::Multiplex mp{&x, select, key};
        msg = _impl::TypeMsg<T>(mp);
        select->_msgs.emplace_back(&msg);
        chan->recv_from(&msg);
      };
      _select->_listeners.emplace_back(std::move(listener));
    }

    void operator<<(T&& x) {
      auto listener = [x, msg = _impl::TypeMsg<T>(), chan = _chan, select = _select, key = _key]() mutable {
        _impl::BaseMsg::Multiplex mp{&x, select, key};
        msg = _impl::TypeMsg<T>(mp);
        select->_msgs.emplace_back(&msg);
        chan->recv_from(&msg);
      };
      _select->_listeners.emplace_back(std::move(listener));
    }

    void operator>>(T& x) {
      auto listener = [&x, msg = _impl::TypeMsg<T>(), chan = _chan, select = _select, key = _key]() mutable {
        _impl::BaseMsg::Multiplex mp{&x, select, key};
        msg = _impl::TypeMsg<T>(mp);
        select->_msgs.emplace_back(&msg);
        chan->send_to(&msg);
      };
      _select->_listeners.emplace_back(std::move(listener));
    }

    void operator>>(Dropout) {
      auto listener = [x = T(), msg = _impl::TypeMsg<T>(), chan = _chan, select = _select, key = _key]() mutable {
        _impl::BaseMsg::Multiplex mp{&x, select, key};
        msg = _impl::TypeMsg<T>(mp);
        select->_msgs.emplace_back(&msg);
        chan->send_to(&msg);
      };
      _select->_listeners.emplace_back(std::move(listener));
    }

   private:
    Case(Select* select, std::shared_ptr<_impl::BaseChannel> chan, int key) : _select(select), _chan(chan), _key(key) {}

    Select* _select;
    std::shared_ptr<_impl::BaseChannel> _chan;
    int _key;
  };

  struct Default {};

  Select() = default;

  Select(const Select&) = delete;

  Select(Select&&) = delete;

  ~Select() { _drop(); }

  /**
   * @param key: The unique key specifing certain channel event. Key should be >= 0
   */
  template <typename T>
  Case<T> on(int key, const Channel<T>& chan) {
    if (key == InvalidSelectKey) {
      throw std::runtime_error("key not allowed");
    }
    return Case<T>(this, chan._chan, key);
  }

  void on(int key, Default);

  Coroutine<int> operator()();

 private:
  static const int InvalidSelectKey = INT_MIN;

  _impl::Spinlock _mtx;
  Semaphore _signal = {0};
  int _key = InvalidSelectKey;
  int _default_key = InvalidSelectKey;

  std::vector<_impl::BaseMsg*> _msgs;
  std::vector<std::function<void()>> _listeners;

  void _drop();
};

/**
 * @brief Spawn `fn` and collect returned value, send it into returned channel
 * @note Returned channel will contain `cgo::Nil{}` if `T=void`
 */
template <typename T, typename V = std::conditional_t<std::is_void_v<T>, Nil, T>>
auto collect(Context& ctx, Coroutine<T> fn) -> Channel<V> {
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
