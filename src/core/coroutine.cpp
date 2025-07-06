#include "core/coroutine.h"

#include <iostream>

namespace cgo::_impl {

BaseFrame::BaseFrame(BaseFrame&& rhs) {
  if (_promise && _promise->_onwer) {
    std::cerr << "coroutine can't be moved anymore after `init()` or `co_await`\n";
    std::exit(EXIT_FAILURE);
  }
  _destroy();
  std::swap(_handler, rhs._handler);
  std::swap(_promise, rhs._promise);
}

void BaseFrame::_destroy() {
  if (_handler) {
    _handler.destroy();
    _handler = nullptr;
  }
}

void BaseFrame::BasePromise::_call_stack_push(BaseFrame::BasePromise* callee) {
  auto caller = _entry->_current;
  caller->_callee = callee;
  callee->_caller = caller;
  callee->_entry = caller->_entry;
  callee->_entry->_current = callee;
}

auto BaseFrame::BasePromise::_call_stack_pop() -> BaseFrame::BasePromise* {
  auto entry = _onwer->_promise->_entry;
  auto current = entry->_current;
  if (current->_caller) {
    current->_caller->_callee = nullptr;
    entry->_current = current->_caller;
    return entry->_current;
  }
  return nullptr;
}

void FrameOperator::_call_stack_create(BaseFrame& entry) const {
  auto promise = entry._promise;
  promise->_onwer = &entry;
  promise->_entry = promise->_current = promise;
}

void FrameOperator::_call_stack_destroy(BaseFrame& entry) const {
  auto promise = entry._promise;
  auto current = promise->_entry->_current;
  while (current) {
    auto handler = current->_onwer->_handler;
    auto next = current->_caller;
    current->_onwer->_handler = nullptr;
    handler.destroy();
    current = next;
  }
}

void FrameOperator::_call_stack_execute(BaseFrame& f) const {
  BaseFrame::BasePromise* entry = f._promise->_entry;
  BaseFrame::BasePromise* next = entry->_current;
  BaseFrame::BasePromise* current = nullptr;
  do {
    current = next;
    current->_onwer->_handler.resume();
    if (auto& ex = current->_error; ex) {
      next = entry->_call_stack_pop();
      if (!next) {
        std::rethrow_exception(ex);
      }
      next->_error = ex;
      entry->_current = next;
      continue;
    }
    if (current->_onwer->_handler.done()) {
      next = entry->_call_stack_pop();
      continue;
    }
    next = entry->_current;
  } while (next && current != next);
}

}  // namespace cgo::_impl
