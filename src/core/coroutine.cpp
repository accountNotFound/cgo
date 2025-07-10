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
  caller->link_back(callee);
  callee->_entry = _entry;
  _entry->_current = callee;
}

auto BaseFrame::BasePromise::_call_stack_pop() -> BaseFrame::BasePromise* {
  auto current = _entry->_current;
  auto next = current->prev();
  if (next) {
    next->unlink_back();
  }
  return _entry->_current = next;
}

void FrameOperator::_call_stack_create(BaseFrame& entry) {
  auto promise = entry._promise;
  promise->_onwer = &entry;
  promise->_entry = promise->_current = promise;
}

void FrameOperator::_call_stack_destroy(BaseFrame& entry) {
  auto promise = entry._promise;
  auto current = promise->_entry->_current;
  while (current) {
    auto handler = current->_onwer->_handler;
    auto next = current->prev();
    current->_onwer->_handler = nullptr;
    handler.destroy();
    current = next;
  }
}

void FrameOperator::_call_stack_execute(BaseFrame& f) {
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
