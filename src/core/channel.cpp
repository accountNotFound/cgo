#include "core/channel.h"

#include <random>

namespace cgo::_impl::_chan {

MessageBase::Result MessageBase::transfer(MessageBase& src, MessageBase& dst, MoveFn move_fn) {
  Spinlock* m1 = src._mtx;
  Spinlock* m2 = dst._mtx;
  if (reinterpret_cast<size_t>(m1) > reinterpret_cast<size_t>(m2)) {
    std::swap(m1, m2);
  }
  std::unique_lock<Spinlock> guard1, guard2;
  if (m1) {
    guard1 = std::unique_lock(*m1);
  }
  if (m2) {
    guard2 = std::unique_lock(*m2);
  }
  void* from = src.data();
  void* to = dst.data();
  if (from && to) {
    move_fn(from, to);
    src.commit();
    dst.commit();
  }
  return {static_cast<bool>(from), static_cast<bool>(to)};
}

bool MessageMatcher::_send_to(MessageBase& dst, MoveFn move_fn) {
  while (!this->_senders.empty()) {
    auto& sender = *this->_senders.front();
    auto [send_ok, recv_ok] = MessageBase::transfer(sender, dst, move_fn);
    if (send_ok && recv_ok) {
      this->_senders.pop();
      return true;
    } else if (send_ok && !recv_ok) {
      return false;
    } else if (!send_ok && recv_ok) {
      this->_senders.pop();
      continue;
    } else {
      this->_senders.pop();
      break;
    }
  }
  return false;
}

bool MessageMatcher::_recv_from(MessageBase& src, MoveFn move_fn) {
  while (!this->_receivers.empty()) {
    auto& receiver = *this->_receivers.front();
    auto [send_ok, recv_ok] = MessageBase::transfer(src, receiver, move_fn);
    if (send_ok && recv_ok) {
      this->_receivers.pop();
      return true;
    } else if (!send_ok && recv_ok) {
      return false;
    } else if (send_ok && !recv_ok) {
      this->_receivers.pop();
      continue;
    } else {
      this->_receivers.pop();
      break;
    }
  }
  return false;
}

void SelectMsg::commit() {
  this->_target->done = true;
  this->_target->rkey = this->_skey;
  this->_target->sem.release();
}

}  // namespace cgo::_impl::_chan

namespace cgo {

Coroutine<int> Select::operator()(bool with_default) {
  std::minstd_rand rng;
  std::shuffle(this->_invokers.begin(), this->_invokers.end(), rng);
  for (auto& fn : this->_invokers) {
    fn();
    if (this->_target->done) {
      break;
    }
  }
  if (with_default) {
    std::unique_lock guard(this->_target->mtx);
    this->_target->done = true;
    co_return std::move(this->_target->rkey);
  }
  co_await this->_target->sem.aquire();
  co_return std::move(this->_target->rkey);
}

}  // namespace cgo
