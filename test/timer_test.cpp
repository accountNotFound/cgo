#include "cgo.h"

const size_t exec_num = 4;
const size_t foo_num = 10000;
const size_t foo_loop = 100;

cgo::Mutex mutex;
size_t end_num = 0;

cgo::Coroutine<void> foo(std::string name) {
  for (int i = 0; i < foo_loop; i++) {
    co_await cgo::sleep(100);
  }
  co_await mutex.lock();
  end_num++;
  mutex.unlock();
}

cgo::Coroutine<void> foo2(std::string name) {
  for (int i = 0; i < foo_loop; i++) {
    try {
      co_await cgo::timeout(cgo::sleep(1000), 100);
    } catch (const cgo::TimeoutException& e) {
      ;
    }
  }
  co_await mutex.lock();
  end_num++;
  mutex.unlock();
}

int main() {
  cgo::Context ctx;

  ctx.start(exec_num);
  for (int i = 0; i < foo_num; i++) {
    std::string name = "foo_" + std::to_string(i);
    cgo::spawn(foo(name));
  }

  int prev_end_num = 0;
  ctx.loop([&prev_end_num]() {
    int target_num = foo_num;
    if ((end_num - prev_end_num) * 10 > target_num) {
      printf("progress: %lu/%d\n", end_num, target_num);
      prev_end_num = end_num;
    }
    return end_num < target_num;
  });
  if (end_num != foo_num) {
    return -1;
  }
  ctx.stop();
}