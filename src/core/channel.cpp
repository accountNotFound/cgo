#include "core/channel.h"

namespace cgo::_impl::_chan {

bool ChannelBase::send(std::shared_ptr<ValueBase>& src) {
  std::unique_lock guard(this->_mtx);
  if (this->_send_to_buffer(src)) {
    return true;
  }
  if (this->_send_to_reciever(src)) {
    return true;
  }
  this->_senders.emplace(src->weak_from_this());
  return false;
}

bool ChannelBase::recv(std::shared_ptr<ValueBase>& dst) {
  std::unique_lock guard(this->_mtx);
  if (this->_recv_from_buffer(dst)) {
    return true;
  }
  if (this->_recv_from_sender(dst)) {
    return true;
  }
  this->_recievers.emplace(dst->weak_from_this());
  return false;
}

bool ChannelBase::_send_to_buffer(std::shared_ptr<ValueBase>& src) {
  if (this->_size == this->_capacity) {
    return false;
  }
  std::unique_lock<Spinlock> guard;
  if (auto mtx = src->mutex(); mtx) {
    guard = std::unique_lock<Spinlock>(*mtx);
  }
  this->_size++;
  this->_send_to_buffer_impl(src->data());
  src->commit(true);

  // notify reciever
  while (!this->_recievers.empty()) {
    auto reciever = std::move(this->_recievers.front());
    this->_recievers.pop();
    if (auto dst = reciever.lock(); dst) {
      if (this->_recv_from_buffer(dst)) {
        break;
      }
    }
  }
  return true;
}

bool ChannelBase::_recv_from_buffer(std::shared_ptr<ValueBase>& dst) {
  if (this->_size == 0) {
    return false;
  }
  std::unique_lock<Spinlock> guard;
  if (auto mtx = dst->mutex(); mtx) {
    guard = std::unique_lock<Spinlock>(*mtx);
  }
  this->_size--;
  this->_recv_from_buffer_impl(dst->data());
  dst->commit(true);

  // notify reciever
  while (!this->_senders.empty()) {
    auto sender = std::move(this->_senders.front());
    this->_senders.pop();
    if (auto src = sender.lock(); src) {
      if (this->_send_to_buffer(src)) {
        break;
      }
    }
  }
  return true;
}

bool ChannelBase::_send_to_reciever(std::shared_ptr<ValueBase>& src) {
  while (!this->_recievers.empty()) {
    auto reciever = std::move(this->_recievers.front());
    this->_recievers.pop();
    if (auto dst = reciever.lock(); dst) {
      if (this->_transmit(src, dst)) {
        return true;
      }
    }
  }
  return false;
}

bool ChannelBase::_recv_from_sender(std::shared_ptr<ValueBase>& dst) {
  while (!this->_senders.empty()) {
    auto sender = std::move(this->_senders.front());
    this->_senders.pop();
    if (auto src = sender.lock(); dst) {
      if (this->_transmit(src, dst)) {
        return true;
      }
    }
  }
  return false;
}

bool ChannelBase::_transmit(std::shared_ptr<ValueBase>& src, std::shared_ptr<ValueBase>& dst) {
  std::unique_lock<Spinlock> guard1;
  std::unique_lock<Spinlock> guard2;

  // avoid dead lock
  auto mtx1 = src->mutex();
  auto mtx2 = dst->mutex();
  if (reinterpret_cast<size_t>(mtx1) > reinterpret_cast<size_t>(mtx2)) {
    std::swap(mtx1, mtx2);
  }
  if (mtx1) {
    guard1 = std::unique_lock(*mtx1);
  }
  if (mtx2) {
    guard2 = std::unique_lock(*mtx2);
  }

  if (void *from = src->data(), *to = dst->data(); from && to) {
    this->_transmit_impl(from, to);
    src->commit(true);
    dst->commit(true);
    return true;
  }
  return false;
}

}  // namespace cgo::_impl::_chan
