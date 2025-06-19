#include "core/coroutine.h"

namespace cgo::_impl::_coro {

void init(CoroutineBase& fn) {
  auto& stk = fn._promise()._stack = std::make_shared<std::stack<std::coroutine_handle<>>>();
  stk->push(fn._handler());
}

bool done(CoroutineBase& fn) { return fn._handler().done(); }

void resume(CoroutineBase& fn) {
  auto& stk = *fn._promise()._stack;
  std::coroutine_handle<> current;
  do {
    current = stk.top();
    current.resume();
    if (auto& ex = fn._promise()._exception; ex) {
      stk.pop();
      if (stk.empty()) {
        std::rethrow_exception(ex);
      }
      using P = Coroutine<void>::promise_type;
      std::coroutine_handle<P>::from_address(stk.top().address()).promise()._exception = ex;
      continue;
    }
    if (current.done()) {
      stk.pop();
      continue;
    }
  } while (!stk.empty() && stk.top() != current);
}

}  // namespace cgo::_impl::_coro
