#include <chrono>
#include <string>

#include "aio/atime.h"
#include "core/condition.h"
#include "core/executor.h"

// #define USE_DEBUG
// #define USE_ASSERT
#include "util/format.h"

const size_t exec_num = 4;
const size_t foo_num = 10000;
const size_t foo_loop = 10;

cgo::Mutex mutex;
size_t end_num = 0;

cgo::Channel<int> bar(unsigned long long timeout_ms) {
  cgo::Channel<int> res;
  cgo::spawn([](cgo::Channel<int> chan, unsigned long long timeout_ms) -> cgo::Coroutine<void> {
    co_await cgo::sleep(timeout_ms);
    co_await chan.send(timeout_ms);
  }(res, timeout_ms));
  return res;
}

cgo::Coroutine<void> foo(std::string name) {
  auto chan1 = bar(1000);
  auto chan2 = bar(2000);
  auto chan3 = bar(3000);
  for (int i = 0; i < foo_loop; i++) {
    cgo::Timer timer(1500);
    for (cgo::Selector s;;) {
      if (s.test(chan3)) {
        int res = s.cast<int>();
        DEBUG("%s select %d\n", name.data(), res);
        break;
      } else if (s.test(chan2)) {
        int res = s.cast<int>();
        DEBUG("%s select %d\n", name.data(), res);
        break;
      } else if (s.test(chan1)) {
        int res = s.cast<int>();
        DEBUG("%s select %d\n", name.data(), res);
        break;
      } else if (s.test(timer.chan())) {
        void* res = s.cast<void*>();
        DEBUG("%s select %p\n", name.data(), res);
        break;
      } else {
        // break
        co_await s.wait();
      }
    }
    // DEBUG("%s: %d/%d select done\n", name.data(), i, foo_loop);
  }
  co_await mutex.lock();
  end_num += 1;
  mutex.unlock();
}

int main() {
  cgo::_impl::ScheduleContext ctx;
  cgo::_impl::EventHandler handler;
  cgo::_impl::TaskExecutor exec(&ctx, &handler);

  exec.start(exec_num);
  for (int i = 0; i < foo_num; i++) {
    std::string name = "foo_" + std::to_string(i);
    cgo::spawn(foo(name));
  }

  size_t target_num = foo_num;
  while (end_num < target_num) {
    handler.handle();
  }
  if (end_num != target_num) {
    return -1;
  }
  exec.stop();
}