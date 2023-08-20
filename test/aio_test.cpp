#include <chrono>

#include "aio/atime.h"
#include "core/channel.h"

// #define USE_DEBUG
#include "util/log.h"

using namespace cgo::impl;

namespace time_test {

const size_t exec_num = 4;
size_t foo_num = 3000, foo_loop = 100;
size_t end_num = 0;

constexpr long sleep_millisec = 50;

Context ctx;
Mutex lock;

Async<void> foo(std::string name) {
  long ts = 0;
  DEBUG("foo {%s} begin", name.data());
  for (int i = 0; i < foo_loop; ++i) {
    co_await cgo::impl::sleep(sleep_millisec + i);
    ts += sleep_millisec + i;
    // DEBUG("foo {%s} at {%u} millisec", name.data(), ts);
  }
  DEBUG("foo {%s} end", name.data());
  co_await lock.lock();
  end_num++;
  DEBUG("end_um=%d", end_num);
  lock.unlock();
}

std::chrono::milliseconds current_millisec() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
}

int test() {
  ctx.start(exec_num);
  auto begin_ms = current_millisec();
  for (int i = 0; i < foo_num; i++) {
    ctx.spawn(foo("foo_" + std::to_string(i)));
  }
  while (end_num < foo_num) {
    ctx.handler().handle();
  }
  ctx.stop();
  auto end_ms = current_millisec();
  size_t theory_ms_cost = (sleep_millisec + sleep_millisec + foo_loop) * foo_loop / 2;
  printf("theory time cost: %u\n", theory_ms_cost);
  printf("real time cost: %u\n", (end_ms - begin_ms).count());
  if (end_ms - begin_ms > std::chrono::milliseconds(theory_ms_cost * 4 / 3) ||
      end_ms - begin_ms < std::chrono::microseconds(theory_ms_cost)) {
    return -1;
  }
  return 0;
}

}  // namespace time_test

int main() {
  int code = time_test::test();
  return code;
}