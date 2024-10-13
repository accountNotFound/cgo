#include "core/channel.h"

#include <random>

namespace cgo::_impl::_chan {

void SelectReducer::commit(int key) {
  this->key = key;
  this->done = true;
  this->sem.release();
}

ChanEvent::MoveStatus ChanEvent::to(void* dst, MoveFn move_fn) {
  if (this->_select_reduce) {
    std::unique_lock guard(this->_select_reduce->mtx);
    if (this->_select_reduce->done) {
      return MoveStatus::SrcInvalid;
    }
    move_fn(this->_data, dst);
    this->_select_reduce->commit(this->_select_key);
    return MoveStatus::Ok;
  } else {
    move_fn(this->_data, dst);
    this->_chan_sem->release();
    return MoveStatus::Ok;
  }
}

ChanEvent::MoveStatus ChanEvent::from(void* src, MoveFn move_fn) {
  if (this->_select_reduce) {
    std::unique_lock guard(this->_select_reduce->mtx);
    if (this->_select_reduce->done) {
      return MoveStatus::DstInvalid;
    }
    move_fn(src, this->_data);
    this->_select_reduce->commit(this->_select_key);
    return MoveStatus::Ok;
  } else {
    move_fn(src, this->_data);
    this->_chan_sem->release();
    return MoveStatus::Ok;
  }
}

ChanEvent::MoveStatus ChanEvent::to(ChanEvent& dst, MoveFn move_fn) {
  if (this->_select_reduce && dst._select_reduce) {
    auto* m1 = &this->_select_reduce->mtx;
    auto* m2 = &dst._select_reduce->mtx;
    if (m1 == m2) {
      // throw
    }
    if (reinterpret_cast<size_t>(m1) > reinterpret_cast<size_t>(m2)) {
      std::swap(m1, m2);
    }
    std::unique_lock guard1(*m1);
    std::unique_lock guard2(*m2);
    if (this->_select_reduce->done) {
      return MoveStatus::SrcInvalid;
    } else if (dst._select_reduce->done) {
      return MoveStatus::DstInvalid;
    }
    move_fn(this->_data, dst._data);
    this->_select_reduce->commit(this->_select_key);
    dst._select_reduce->commit(dst._select_key);
    return MoveStatus::Ok;
  } else if (this->_select_reduce && !dst._select_reduce) {
    auto res = this->to(dst._data, move_fn);
    if (res == MoveStatus::Ok) {
      dst._chan_sem->release();
    }
    return res;
  } else {
    auto res = dst.from(this->_data, move_fn);
    if (res == MoveStatus::Ok) {
      this->_chan_sem->release();
    }
    return res;
  }
}

bool ChanEventMatcher::to(void* dst, MoveFn move_fn) {
  while (!this->_senders.empty()) {
    auto res = this->_senders.front().to(dst, move_fn);
    if (res == ChanEvent::MoveStatus::Ok) {
      this->_senders.pop();
      return true;
    } else if (res == ChanEvent::MoveStatus::SrcInvalid) {
      this->_senders.pop();
      continue;
    } else {
      break;
    }
  }
  return false;
}

bool ChanEventMatcher::from(void* src, MoveFn move_fn) {
  while (!this->_receivers.empty()) {
    auto res = this->_receivers.front().from(src, move_fn);
    if (res == ChanEvent::MoveStatus::Ok) {
      this->_receivers.pop();
      return true;
    } else if (res == ChanEvent::MoveStatus::DstInvalid) {
      this->_receivers.pop();
      continue;
    } else {
      break;
    }
  }
  return false;
}

bool ChanEventMatcher::to(ChanEvent& dst, MoveFn move_fn) {
  while (!this->_senders.empty()) {
    auto res = this->_senders.front().to(dst, move_fn);
    if (res == ChanEvent::MoveStatus::Ok) {
      this->_senders.pop();
      return true;
    } else if (res == ChanEvent::MoveStatus::SrcInvalid) {
      this->_senders.pop();
      continue;
    } else {
      break;
    }
  }
  return false;
}

bool ChanEventMatcher::from(ChanEvent& src, MoveFn move_fn) {
  while (!this->_receivers.empty()) {
    auto res = this->_receivers.front().from(src, move_fn);
    if (res == ChanEvent::MoveStatus::Ok) {
      this->_receivers.pop();
      return true;
    } else if (res == ChanEvent::MoveStatus::DstInvalid) {
      this->_receivers.pop();
      continue;
    } else {
      break;
    }
  }
  return false;
}

}  // namespace cgo::_impl::_chan

namespace cgo {

Coroutine<int> Select::operator()(bool with_default) {
  std::minstd_rand rng;
  std::shuffle(this->_invokers.begin(), this->_invokers.end(), rng);
  for (auto& fn : this->_invokers) {
    fn();
    if (this->_reducer->done) {
      break;
    }
  }
  if (!with_default) {
    co_await this->_reducer->sem.aquire();
  }
  std::unique_lock guard(this->_reducer->mtx);
  this->_reducer->done = true;
  co_return std::move(this->_reducer->key);
}

}  // namespace cgo
