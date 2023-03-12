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

auto AsyncTrait::start() -> void {
  this->_stack = std::make_shared<std::stack<HandlerTrait>>();
  this->_stack->push(this->_handler);
  DEBUG("start: this=%p, stack=%p\n", this, this->_stack.get());
}

auto AsyncTrait::resume() -> void {
  while (!this->_stack->empty() && this->_stack->top().done()) {
    this->_stack->pop();
  }
  this->_stack->top().resume();
}

auto AsyncTrait::done() const -> bool { return this->_handler.done(); }

auto AsyncTrait::call(AsyncTrait& callee) -> void {
  DEBUG("call: this=%p, callee=%p, stack=%p\n", this, &callee, this->_stack.get());
  this->_stack->push(callee._handler);
  callee._stack = this->_stack;
}

}  // namespace cgo::impl
