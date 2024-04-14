#include "./coroutine.h"

#define USE_ASSERT
#include "../util/format.h"
#undef USE_ASSERT

namespace cgo::_impl {

CoroutineBase::CoroutineBase(CoroutineBase&& rhs) {
  if (this->_promise && !this->_promise->co_handler.done()) {
    this->_promise->co_handler.destroy();
  }
  this->_promise = rhs._promise;
  rhs._promise = nullptr;
}

CoroutineBase::~CoroutineBase() {
  if (this->_promise && !this->_promise->co_handler.done()) {
    this->_promise->co_handler.destroy();
  }
  this->_promise = nullptr;
}

void CoroutineBase::start() {
  ASSERT(!this->_promise->co_frames, "coroutine start twice, this=%p", this);
  this->_promise->co_frames = std::make_shared<std::stack<_impl::CoroutinePromiseBase*>>();
  this->_promise->co_frames->push(this->_promise);
}

void CoroutineBase::resume() {
  ASSERT(this->_promise->co_frames && !this->_promise->co_frames->empty(),
         "invalid coroutine resume, maybe it is not started, this=%p", this);
  auto f = this->_promise->co_frames->top();
  if (f->error) {
    this->_promise->co_frames->pop();
    f = this->_promise->co_frames->top();
  }
  f->co_handler.resume();
  if (f->co_handler.done() || f->error) {
    this->_promise->co_frames->pop();
  }
  if (this->_promise->co_frames->empty() && f->error) {
    std::rethrow_exception(f->error);
  }
}

bool CoroutineBase::done() { return this->_promise->co_handler.done(); }

}  // namespace cgo::_impl
