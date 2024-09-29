#pragma once

#include <functional>
#include <vector>

#include "core/schedule.h"

namespace cgo::_impl::_chan {

using MoveFn = void (*)(void*, void*);

class MessageBase {
 public:
  struct Result {
   public:
    bool send_ok;
    bool recv_ok;
  };

  MessageBase() : _mtx(nullptr) {}

  MessageBase(Spinlock& mtx) : _mtx(&mtx) {}

  virtual ~MessageBase() = default;

  static Result transfer(MessageBase& src, MessageBase& dst, MoveFn);

  template <typename T>
  Result send_to(MessageBase& dst) {
    return MessageBase::transfer(
        *this, dst, [](void* src, void* dst) { *static_cast<T*>(dst) = std::move(*static_cast<T*>(src)); });
  }

  template <typename T>
  Result recv_from(MessageBase& src) {
    return MessageBase::transfer(
        src, *this, [](void* src, void* dst) { *static_cast<T*>(dst) = std::move(*static_cast<T*>(src)); });
  }

  virtual void* data() = 0;

  virtual void commit() = 0;

 private:
  Spinlock* _mtx;
};

class MessageMatcher {
 public:
  void submit_sender(const std::shared_ptr<MessageBase>& sender) { this->_senders.push(std::move(sender)); }

  void submit_receiver(const std::shared_ptr<MessageBase>& receiver) { this->_receivers.push(std::move(receiver)); }

  template <typename T>
  bool send_to(MessageBase& dst) {
    return this->_send_to(dst, [](void* src, void* dst) { *static_cast<T*>(dst) = std::move(*static_cast<T*>(src)); });
  }

  template <typename T>
  bool recv_from(MessageBase& src) {
    return this->_recv_from(src,
                            [](void* src, void* dst) { *static_cast<T*>(dst) = std::move(*static_cast<T*>(src)); });
  }

 private:
  std::queue<std::shared_ptr<MessageBase>> _senders, _receivers;

  bool _send_to(MessageBase& dst, MoveFn move_fn);

  bool _recv_from(MessageBase& src, MoveFn move_fn);
};

template <typename T>
class BufferSendMsg : public MessageBase {
 public:
  BufferSendMsg(std::queue<T>& buffer) : _buffer(&buffer) {}

  void* data() override { return &this->_buffer->front(); }

  void commit() override { this->_buffer->pop(); }

 private:
  std::queue<T>* _buffer;
};

template <typename T>
class BufferRecvMsg : public MessageBase {
 public:
  BufferRecvMsg(std::queue<T>& buffer) : _buffer(&buffer) {}

  void* data() override { return &this->_value; }

  void commit() override { this->_buffer->push(std::move(this->_value)); }

 private:
  std::queue<T>* _buffer;
  T _value;
};

template <typename T>
class ChannelImpl {
 public:
  ChannelImpl(size_t capacity) : _capacity(capacity) {}

  void submit_sender(const std::shared_ptr<MessageBase>& sender) {
    std::unique_lock guard(this->_mtx);
    if (this->_buffer.size() < this->_capacity) {
      auto buf_tail = BufferRecvMsg(this->_buffer);
      if (auto [send_ok, _] = sender->send_to<T>(buf_tail); send_ok) {
        auto buf_head = BufferSendMsg(this->_buffer);
        this->_matcher.recv_from<T>(buf_head);
      }
      return;
    }
    if (this->_capacity == 0 && this->_matcher.recv_from<T>(*sender)) {
      return;
    }
    this->_matcher.submit_sender(sender);
  }

  void submit_receiver(const std::shared_ptr<MessageBase>& receiver) {
    std::unique_lock guard(this->_mtx);
    if (!this->_buffer.empty()) {
      auto buf_head = BufferSendMsg(this->_buffer);
      if (auto [_, recv_ok] = receiver->recv_from<T>(buf_head); recv_ok) {
        auto buf_tail = BufferRecvMsg(this->_buffer);
        this->_matcher.send_to<T>(buf_tail);
      }
      return;
    }
    if (this->_capacity == 0 && this->_matcher.send_to<T>(*receiver)) {
      return;
    }
    this->_matcher.submit_receiver(receiver);
  }

 private:
  const size_t _capacity;
  Spinlock _mtx;
  std::queue<T> _buffer;
  MessageMatcher _matcher;
};

class ChannelMsg : public MessageBase {
 public:
  ChannelMsg(void* data) : _data(data), _sem(0) {}

  void* data() override { return this->_data; }

  void commit() override { this->_sem.release(); }

  Coroutine<void> wait() { return this->_sem.aquire(); }

 private:
  void* _data;
  Semaphore _sem;
};

class SelectMsg : public MessageBase {
 public:
  struct Target {
   public:
    Spinlock mtx;
    int rkey = -1;
    Semaphore sem = {0};
    bool done = false;
  };

  SelectMsg(std::shared_ptr<Target> target, int skey, void* data)
      : MessageBase(target->mtx), _target(target), _skey(skey), _data(data) {}

  void bind(void* data) { this->_data = data; }

  void* data() override { return this->_target->done || this->_target->rkey != -1 ? nullptr : this->_data; }

  void commit() override;

 private:
  std::shared_ptr<Target> _target;
  int _skey;
  void* _data;
};

}  // namespace cgo::_impl::_chan

namespace cgo {

template <typename T>
class Channel {
  friend class Select;

 public:
  using ValueType = T;

  Channel(size_t capacity = 0) : _impl(std::make_shared<_impl::_chan::ChannelImpl<T>>(capacity)) {}

  Coroutine<void> operator<<(const T& x) {
    T x_copy = x;
    _impl::_chan::ChannelMsg msg(&x_copy);
    std::shared_ptr<_impl::_chan::MessageBase> sender(&msg, [](auto* p) {});
    this->_impl->submit_sender(sender);
    co_await msg.wait();
  }

  Coroutine<void> operator<<(T&& x) {
    _impl::_chan::ChannelMsg msg(&x);
    std::shared_ptr<_impl::_chan::MessageBase> sender(&msg, [](auto* p) {});
    this->_impl->submit_sender(sender);
    co_await msg.wait();
  }

  Coroutine<void> operator>>(T& x) {
    _impl::_chan::ChannelMsg msg(&x);
    std::shared_ptr<_impl::_chan::MessageBase> receiver(&msg, [](auto* p) {});
    this->_impl->submit_receiver(receiver);
    co_await msg.wait();
  }

 private:
  std::shared_ptr<_impl::_chan::ChannelImpl<T>> _impl;
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
    Case(Select& select, _impl::_chan::ChannelImpl<T>& chan, std::shared_ptr<_impl::_chan::SelectMsg> msg)
        : _select(&select), _chan(&chan), _msg(msg) {}

    void operator<<(const T& x) {
      this->_select->_invokers.emplace_back([x, chan = _chan, msg = _msg]() {
        msg->bind(const_cast<T*>(&x));
        chan->submit_sender(std::dynamic_pointer_cast<_impl::_chan::MessageBase>(msg));
      });
    }

    void operator<<(T&& x) {
      this->_select->_invokers.emplace_back([x = std::move(x), chan = _chan, msg = _msg]() {
        msg->bind(const_cast<T*>(&x));
        chan->submit_sender(std::dynamic_pointer_cast<_impl::_chan::MessageBase>(msg));
      });
    }

    void operator>>(T& x) {
      this->_select->_invokers.emplace_back([&x, chan = _chan, msg = _msg]() {
        msg->bind(&x);
        chan->submit_receiver(std::dynamic_pointer_cast<_impl::_chan::MessageBase>(msg));
      });
    }

   private:
    Select* _select;
    _impl::_chan::ChannelImpl<T>* _chan;
    std::shared_ptr<_impl::_chan::SelectMsg> _msg;
  };

  Select() : _target(std::make_shared<_impl::_chan::SelectMsg::Target>()) {}

  Select(const Select&) = delete;

  /**
   * @param key: The unique key specifing certain channel event. Key should be >= 0
   */
  template <typename T>
  Case<T> on(int key, Channel<T>& chan) {
    auto msg = std::make_shared<_impl::_chan::SelectMsg>(this->_target, key, nullptr);
    return Case<T>(*this, *chan._impl, msg);
  }

  /**
   * @return Unique key set in `Select::on()`, or -1 if `with_default=true` and no channel event comes to activate
   */
  Coroutine<int> operator()(bool with_default);

 private:
  std::shared_ptr<_impl::_chan::SelectMsg::Target> _target;
  std::vector<std::function<void()>> _invokers;
};

}  // namespace cgo
