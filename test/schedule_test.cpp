#include <chrono>
#include <string>

// #define USE_DEBUG

#include "cgo.h"
#include "util/format.h"

const size_t exec_num = 4;
const size_t foo_num = 1000;
const size_t foo_loop = 1000;

cgo::util::SpinLock mutex;
size_t end_num = 0;

cgo::Coroutine<void> foo(std::string name) {
  for (int i = 0; i < foo_loop; i++) {
    DEBUG("before yield, name=%s, index=%d\n", name.data(), i);
    co_await cgo::yield();
    DEBUG("after yield, name=%s, index=%d\n", name.data(), i);
  }
  std::unique_lock guard(mutex);
  end_num++;
}

int main() {
  cgo::Context ctx;

  ctx.start(exec_num);
  for (int i = 0; i < foo_num; i++) {
    std::string name = "foo_" + std::to_string(i);
    cgo::spawn(foo(name));
  }

  while (end_num < foo_num) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // DEBUG("end_num=%lu, foo_num=%lu", end_num, foo_num);
  }
  if (end_num != foo_num) {
    return -1;
  }
  ctx.stop();
}