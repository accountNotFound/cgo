#include "core/channel.h"

#include <random>

namespace cgo::_impl {

void BaseMsg::Simplex::commit() {
  if (signal) {
    signal->release();
  }
}

void BaseMsg::Multiplex::commit() {
  select->_key = case_key;
  select->_signal.release();
}

auto BaseMsg::recv_from(void* src) -> BaseMsg::TransferStatus {
  if (std::holds_alternative<Multiplex>(this->_msg)) {
    auto& dst_msg = std::get<Multiplex>(this->_msg);
    auto& dst_select = *dst_msg.select;
    std::unique_lock guard(dst_select._mtx);
    if (dst_select._key != Select::InvalidSelectKey) {
      return TransferStatus::InvalidSrc;
    }
    _move(src, dst_msg.data);
    dst_msg.commit();
    return TransferStatus::Ok;
  } else {
    auto& dst_msg = std::get<Simplex>(this->_msg);
    _move(src, dst_msg.data);
    dst_msg.commit();
    return TransferStatus::Ok;
  }
}

auto BaseMsg::send_to(void* dst) -> BaseMsg::TransferStatus {
  if (std::holds_alternative<Multiplex>(this->_msg)) {
    auto& src_msg = std::get<Multiplex>(this->_msg);
    auto& src_select = *src_msg.select;
    std::unique_lock guard(src_select._mtx);
    if (src_select._key != Select::InvalidSelectKey) {
      return TransferStatus::InvalidSrc;
    }
    _move(src_msg.data, dst);
    src_msg.commit();
    return TransferStatus::Ok;
  } else {
    auto& src_msg = std::get<Simplex>(this->_msg);
    _move(src_msg.data, dst);
    src_msg.commit();
    return TransferStatus::Ok;
  }
}

auto BaseMsg::send_to(BaseMsg* dst) -> BaseMsg::TransferStatus {
  if (std::holds_alternative<Multiplex>(this->_msg) && std::holds_alternative<Multiplex>(dst->_msg)) {
    auto& src_msg = std::get<Multiplex>(this->_msg);
    auto& dst_msg = std::get<Multiplex>(dst->_msg);
    auto& src_select = *src_msg.select;
    auto& dst_select = *dst_msg.select;
    auto m1 = &src_select._mtx;
    auto m2 = &dst_select._mtx;
    if (m1 == m2) {
      throw std::runtime_error("selector deadlock because waiting send and recv on the same channel");
    }
    if (reinterpret_cast<size_t>(m1) > reinterpret_cast<size_t>(m2)) {
      std::swap(m1, m2);
    }
    std::unique_lock guard1(*m1), guard2(*m2);
    if (src_select._key != Select::InvalidSelectKey) {
      return TransferStatus::InvalidSrc;
    }
    if (dst_select._key != Select::InvalidSelectKey) {
      return TransferStatus::InvalidDst;
    }
    _move(src_msg.data, dst_msg.data);
    src_msg.commit();
    dst_msg.commit();
    return TransferStatus::Ok;
  } else if (std::holds_alternative<Multiplex>(this->_msg) && std::holds_alternative<Simplex>(dst->_msg)) {
    auto& dst_msg = std::get<Simplex>(dst->_msg);
    auto res = this->send_to(dst_msg.data);
    if (res == TransferStatus::Ok) {
      dst_msg.commit();
    }
    return res;
  } else if (std::holds_alternative<Simplex>(this->_msg) && std::holds_alternative<Multiplex>(dst->_msg)) {
    auto& src_msg = std::get<Simplex>(this->_msg);
    auto res = dst->recv_from(src_msg.data);
    if (res == TransferStatus::Ok) {
      src_msg.commit();
    }
    return res;
  } else {
    auto& src_msg = std::get<Simplex>(this->_msg);
    auto res = dst->recv_from(src_msg.data);
    if (res == TransferStatus::Ok) {
      src_msg.commit();
    }
    return res;
  }
}

void BaseMsg::drop() {
  std::unique_lock guard(_chan->_mtx);
  unlink_this();
}

BaseChannel::BaseChannel() {
  _sender_head.link_back(&_sender_tail);
  _recver_head.link_back(&_recver_tail);
}

auto BaseChannel::send_to(BaseMsg* dst, bool oneshot) -> TransferStatus {
  std::unique_lock guard(_mtx);
  dst->_chan = this;
  if (_buffer_send_to(dst)) {
    while (_sender_head.back() != &_sender_tail) {
      auto sender = _sender_head.unlink_back();
      if (_buffer_recv_from(sender)) {
        break;
      }
    }
    return TransferStatus::Ok;
  }
  while (_sender_head.back() != &_sender_tail) {
    auto status = const_cast<BaseMsg*>(_sender_head.back())->send_to(dst);
    if (status == BaseMsg::TransferStatus::Ok) {
      _sender_head.unlink_back();
      return TransferStatus::Ok;
    } else if (status == BaseMsg::TransferStatus::InvalidSrc) {
      _sender_head.unlink_back();
      continue;
    } else {
      return TransferStatus::InvalidDst;
    }
  }
  if (!oneshot) {
    _recver_tail.link_front(dst);
    return TransferStatus::Inprocess;
  }
  return TransferStatus::InvalidOneshot;
}

auto BaseChannel::recv_from(BaseMsg* src, bool oneshot) -> TransferStatus {
  std::unique_lock guard(_mtx);
  src->_chan = this;
  if (_buffer_recv_from(src)) {
    while (_recver_head.back() != &_recver_tail) {
      auto recver = _recver_head.unlink_back();
      if (_buffer_send_to(recver)) {
        break;
      }
    }
    return TransferStatus::Ok;
  }
  while (_recver_head.back() != &_recver_tail) {
    auto status = const_cast<BaseMsg*>(_recver_head.back())->recv_from(src);
    if (status == BaseMsg::TransferStatus::Ok) {
      _recver_head.unlink_back();
      return TransferStatus::Ok;
    } else if (status == BaseMsg::TransferStatus::InvalidDst) {
      _recver_head.unlink_back();
      continue;
    } else {
      return TransferStatus::InvalidSrc;
    }
  }
  if (!oneshot) {
    _sender_tail.link_front(src);
    return TransferStatus::Inprocess;
  }
  return TransferStatus::InvalidOneshot;
}

}  // namespace cgo::_impl

namespace cgo {

void Select::on(int key, Select::Default) {
  if (_default_key != InvalidSelectKey) {
    throw std::runtime_error("select already has a default case");
  }
  if (key == InvalidSelectKey) {
    throw std::runtime_error("key not allowed");
  }
  _default_key = key;
}

Coroutine<int> Select::operator()() {
  std::minstd_rand rng;
  std::shuffle(_listeners.begin(), this->_listeners.end(), rng);
  for (auto& fn : this->_listeners) {
    fn();
    std::unique_lock guard(_mtx);
    if (_key != InvalidSelectKey) {
      break;
    }
  }

  auto guard = defer([this]() { _drop(); });
  if (_default_key == InvalidSelectKey) {
    co_await _signal.aquire();
    std::unique_lock guard(_mtx);
    co_return _key;
  } else {
    std::unique_lock guard(_mtx);
    if (_key == InvalidSelectKey) {
      _key = _default_key;
    }
    co_return _key;
  }
}

void Select::_drop() {
  for (auto msg : _msgs) {
    msg->drop();
  }
  _msgs.clear();
  _listeners.clear();
}

}  // namespace cgo
