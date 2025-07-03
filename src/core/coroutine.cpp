#include "core/coroutine.h"

#include <iostream>

namespace cgo {

namespace _impl {

namespace _coro {

void PromiseBase::call_stack_create(PromiseBase* promise) { promise->_entry = promise->_current = promise; }

void PromiseBase::call_stack_destroy(PromiseBase* promise) {
  auto current = promise->_entry->_current;
  while (current) {
    current->_callee = nullptr;
    current->coro->handler.destroy();
    current->coro->handler = nullptr;
    current = current->_caller;
  }
}

void PromiseBase::call_stack_execute(PromiseBase* promise) {
  PromiseBase* entry = promise->_entry;
  PromiseBase* next = entry->_current;
  PromiseBase* current = nullptr;
  do {
    current = next;
    current->coro->handler.resume();
    if (auto& ex = current->_exception; ex) {
      next = PromiseBase::_call_stack_pop(entry);
      if (!next) {
        std::rethrow_exception(ex);
      }
      next->_exception = ex;
      entry->_current = next;
      continue;
    }
    if (current->coro->handler.done()) {
      next = PromiseBase::_call_stack_pop(entry);
      continue;
    }
    next = entry->_current;
  } while (next && current != next);
}

void PromiseBase::call_stack_push(PromiseBase* promise, PromiseBase* callee) {
  auto caller = promise->_entry->_current;
  caller->_callee = callee;
  callee->_caller = caller;
  callee->_entry = caller->_entry;
  callee->_entry->_current = callee;
}

auto PromiseBase::_call_stack_pop(PromiseBase* promise) -> PromiseBase* {
  auto entry = promise->_entry;
  auto current = entry->_current;
  if (current->_caller) {
    current->_caller->_callee = nullptr;
    entry->_current = current->_caller;
    return entry->_current;
  }
  return current = nullptr;
}

}  // namespace _coro

CoroutineBase::CoroutineBase(CoroutineBase&& rhs) {
  if (this->_promise->coro) {
    std::cerr << "coroutine can't be moved anymore after `init()` or `co_await`\n";
    std::exit(EXIT_FAILURE);
  }
  this->_destroy_coro_frame();
  std::swap(this->handler, rhs.handler);
  std::swap(this->_promise, rhs._promise);
}

void CoroutineBase::_destroy_coro_frame() {
  if (this->handler) {
    this->handler.destroy();
    this->handler = nullptr;
  }
}

}  // namespace _impl

}  // namespace cgo
