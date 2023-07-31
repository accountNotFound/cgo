#include "async.h"

// #define USE_DEBUG
#include "util/log.h"

namespace cgo::impl {

AsyncTrait::AsyncTrait(AsyncTrait&& rhs) {
  DEBUG("move: this=%p, from=%p\n", this, &rhs);
  this->_handler = rhs._handler;
  this->_promise = rhs._promise;
  this->_stack = rhs._stack;
  rhs._handler = nullptr;
  rhs._promise = nullptr;
  rhs._stack = nullptr;
  this->_promise->set_wrapper(*this);
}

AsyncTrait::~AsyncTrait() {
  if (this->_handler && !this->_handler.done()) {
    this->_handler.destroy();
  }
}

void AsyncTrait::start() {
  this->_stack = std::make_shared<std::stack<AsyncTrait*>>();
  this->_stack->push(this);
  DEBUG("start: this=%p, stack=%p\n", this, this->_stack.get());
}

void AsyncTrait::resume() {
  while (!this->_stack->empty()) {
    if (this->_stack->top()->_handler.done()) {
      this->_stack->pop();
      continue;
    }
    if (this->_stack->top()->_promise->exception()) {
      auto e = this->_stack->top()->_promise->exception();
      this->_stack->pop();
      this->_stack->top()->_promise->set_exception(e);
    }
    break;
  }
  if (!this->_stack->empty()) {
    auto cur = this->_stack->top();
    cur->_handler.resume();
    if (cur == this && cur->_promise->exception()) {
      std::rethrow_exception(cur->_promise->exception());
    }
  }
}

auto AsyncTrait::done() const -> bool { return this->_handler.done(); }

void AsyncTrait::call(AsyncTrait& callee) {
  DEBUG("call: this=%p, callee=%p, stack=%p\n", this, &callee, this->_stack.get());
  this->_stack->push(&callee);
  callee._stack = this->_stack;
}

}  // namespace cgo::impl
