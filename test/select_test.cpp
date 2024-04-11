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

cgo::ReadChannel<int> bar(unsigned long long timeout_ms) {
  cgo::Channel<int> res;
  cgo::spawn([](cgo::Channel<int> chan, unsigned long long timeout_ms) -> cgo::Coroutine<void> {
    co_await cgo::sleep(timeout_ms);
    co_await chan.send(timeout_ms);
  }(res, timeout_ms));
  return res;
}

cgo::Coroutine<void> foo(std::string name) {
  for (int i = 0; i < foo_loop; i++) {
    cgo::Selector s;
    s.on("bar_1"_u, bar(1000))
        .on("bar_2"_u, bar(2000))
        .on("bar_3"_u, bar(3000))
        .on("timeout"_u, cgo::Timer(i < 3 ? 500 : 1500).chan())
        .on("empty"_u, cgo::Channel<int>());

    switch ((co_await s.recv())) {
      case "bar_1"_u:
      case "bar_2"_u:
      case "bar_3"_u: {
        auto v = s.cast<int>();
        DEBUG("%s select %d\n", name.data(), v);
        break;
      }
      case "timeout"_u: {
        void *res = s.cast<void *>();
        DEBUG("%s select %p\n", name.data(), res);
        break;
      }
      case "empty"_u: {
        ASSERT(false, "nerver be here");
        break;
      }
        // if you want a default block, co_await s.wait_or_default(your_default_key))
    }
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