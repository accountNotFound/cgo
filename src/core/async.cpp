#include "async.h"

namespace cgo::impl {

auto AsyncTrait::start() -> void {
  this->_stack = std::make_shared<std::stack<HandlerTrait>>();
  this->_stack->push(this->_handler);
}

auto AsyncTrait::resume() -> void {
  while (!this->_stack->empty() && this->_stack->top().done()) {
    this->_stack->pop();
  }
  this->_stack->top().resume();
}

auto AsyncTrait::done() const -> bool { return this->_handler.done(); }

auto AsyncTrait::call(AsyncTrait& callee) -> void {
  this->_stack->push(callee._handler);
  callee._stack = this->_stack;
}

}  // namespace cgo::impl
