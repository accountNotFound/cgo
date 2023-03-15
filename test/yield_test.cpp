#include <chrono>
#include <string>
#include <thread>

#include "core/async.h"
#include "core/context.h"
#include "util/spin_lock.h"

// #define USE_DEBUG
#include "util/log.h"

using namespace cgo::impl;

const size_t exec_num = 10;
const size_t foo_num = 1000;
const size_t foo_loop = 1000;

SpinLock mutex;
size_t end_num = 0;

Async<void> foo(const std::string& name) {
  for (int i = 0; i < foo_loop; i++) {
    DEBUG("before yield, name=%s, index=%d\n", name.data(), i);
    co_await Context::current().yield();
    DEBUG("after yield, name=%s, index=%d\n", name.data(), i);
  }
  std::unique_lock guard(mutex);
  end_num++;
}

int main() {
  Context ctx;
  ctx.start(exec_num);
  for (int i = 0; i < foo_num; i++) {
    std::string name = "foo_" + std::to_string(i);
    ctx.spawn(foo(name), name);
  }
  while (end_num < foo_num) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    DEBUG("end_num=%u, foo_num=%u", end_num, foo_num);
  }
  if (end_num != foo_num) {
    return -1;
  }
  ctx.stop();
}