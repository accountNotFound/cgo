#include "core/coroutine.h"

namespace cgo::_impl::_coro {

void PromiseBase::init(PromiseBase* promise) { promise->_entry = promise->_current = promise; }

void PromiseBase::resume(PromiseBase* entry) {
  PromiseBase* next = entry->_current;
  PromiseBase* current = nullptr;
  do {
    current = next;
    current->_handler.resume();
    if (auto& ex = current->_exception; ex) {
      next = PromiseBase::call_pop(current);
      if (!next) {
        std::rethrow_exception(ex);
      }
      next->_exception = ex;
      entry->_current = next;
      continue;
    }
    if (current->_handler.done()) {
      next = PromiseBase::call_pop(current);
      continue;
    }
    next = entry->_current;
  } while (next && current != next);
}

void PromiseBase::call_push(PromiseBase* caller, PromiseBase* callee) {
  caller->_callee = callee;
  callee->_caller = caller;
  callee->_entry = caller->_entry;
  callee->_entry->_current = callee;
}

PromiseBase* PromiseBase::call_pop(PromiseBase* promise) {
  if (promise->_caller) {
    promise->_caller->_callee = nullptr;
    promise->_entry = promise->_caller;
    return promise->_caller;
  }
  return nullptr;
}

}  // namespace cgo::_impl::_coro
