#include "core/channel.h"

#include <random>

namespace cgo::_impl {

void BaseMsg::Multiplex::_commit(int key) {
  *_key = key;
  _signal->release();
}

auto BaseMsg::recv_from(void* src) -> BaseMsg::TransferStatus {
  auto dst = this;
  if (dst->_multiplex) {
    std::unique_lock guard(*dst->_multiplex->_mtx);
    if (*dst->_multiplex->_key != Multiplex::InvalidKey) {
      return TransferStatus::InvalidSrc;
    }
    _move(src, dst->_multiplex->_data);
    dst->_multiplex->_commit(dst->_case_key);
    return TransferStatus::Ok;
  } else {
    _move(src, dst->_simplex->data);
    if (dst->_simplex->signal) {
      dst->_simplex->signal->release();
    }
    return TransferStatus::Ok;
  }
}

auto BaseMsg::send_to(void* dst) -> BaseMsg::TransferStatus {
  auto src = this;
  if (src->_multiplex) {
    std::unique_lock guard(*src->_multiplex->_mtx);
    if (*src->_multiplex->_key != Multiplex::InvalidKey) {
      return TransferStatus::InvalidSrc;
    }
    _move(src->_multiplex->_data, dst);
    src->_multiplex->_commit(src->_case_key);
    return TransferStatus::Ok;
  } else {
    _move(src->_simplex->data, dst);
    if (src->_simplex->signal) {
      src->_simplex->signal->release();
    }
    return TransferStatus::Ok;
  }
}

auto BaseMsg::send_to(BaseMsg* dst) -> BaseMsg::TransferStatus {
  auto src = this;
  if (src->_multiplex && dst->_multiplex) {
    auto m1 = src->_multiplex->_mtx;
    auto m2 = dst->_multiplex->_mtx;
    if (m1 == m2) {
      throw std::runtime_error("selector deadlock because waiting send and recv on the same channel");
    }
    if (reinterpret_cast<size_t>(m1) > reinterpret_cast<size_t>(m2)) {
      std::swap(m1, m2);
    }
    std::unique_lock guard1(*m1), guard2(*m2);
    if (*src->_multiplex->_key != Multiplex::InvalidKey) {
      return TransferStatus::InvalidSrc;
    }
    if (*dst->_multiplex->_key != Multiplex::InvalidKey) {
      return TransferStatus::InvalidDst;
    }
    _move(src->_multiplex->_data, dst->_multiplex->_data);
    src->_multiplex->_commit(src->_case_key);
    dst->_multiplex->_commit(dst->_case_key);
    return TransferStatus::Ok;
  } else if (src->_multiplex && dst->_simplex) {
    auto res = src->send_to(dst->_simplex->data);
    if (res == TransferStatus::Ok && dst->_simplex->signal) {
      dst->_simplex->signal->release();
    }
    return res;
  } else if (src->_simplex && dst->_multiplex) {
    auto res = dst->recv_from(src->_simplex->data);
    if (res == TransferStatus::Ok && src->_simplex->signal) {
      src->_simplex->signal->release();
    }
    return res;
  } else {
    auto res = dst->recv_from(src->_simplex->data);
    if (res == TransferStatus::Ok && src->_simplex->signal) {
      src->_simplex->signal->release();
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
    while (_sender_head.next() != &_sender_tail) {
      auto sender = _sender_head.unlink_back();
      if (_buffer_recv_from(sender)) {
        break;
      }
    }
    return TransferStatus::Ok;
  }
  while (_sender_head.next() != &_sender_tail) {
    auto status = const_cast<BaseMsg*>(_sender_head.next())->send_to(dst);
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
    while (_recver_head.next() != &_recver_tail) {
      auto recver = _recver_head.unlink_back();
      if (_buffer_send_to(recver)) {
        break;
      }
    }
    return TransferStatus::Ok;
  }
  while (_recver_head.next() != &_recver_tail) {
    auto status = const_cast<BaseMsg*>(_recver_head.next())->recv_from(src);
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

Coroutine<int> Select::operator()() {
  std::minstd_rand rng;
  std::shuffle(_listeners.begin(), this->_listeners.end(), rng);
  for (auto& fn : this->_listeners) {
    fn();
    std::unique_lock guard(_mtx);
    if (_key != _impl::BaseMsg::Multiplex::InvalidKey) {
      break;
    }
  }
  if (!_enable_default) {
    co_await _signal.aquire();
  }
  for (auto msg : _msgs) {
    msg->drop();
  }
  std::unique_lock guard(_mtx);
  co_return _key;
}

}  // namespace cgo
