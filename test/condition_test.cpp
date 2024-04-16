#include <chrono>
#include <string>

// #define USE_DEBUG
#include "cgo.h"
#include "util/format.h"

const size_t exec_num = 4;
const size_t foo_num = 1000;
const size_t foo_loop = 1000;

cgo::Mutex mutex;
size_t end_num = 0;

cgo::Coroutine<void> foo(std::string name) {
  for (int i = 0; i < foo_loop; i++) {
    co_await mutex.lock();
    end_num++;
    DEBUG("%s inc, end_num=%lu\n", name.data(), end_num);
    co_await cgo::yield();
    mutex.unlock();
  }
}

int main() {
  cgo::Context ctx;

  ctx.start(exec_num);
  for (int i = 0; i < foo_num; i++) {
    std::string name = "foo_" + std::to_string(i);
    cgo::spawn(foo(name));
  }

  size_t target_num = foo_num * foo_loop;
  while (end_num < target_num) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    DEBUG("end_num=%lu, target_num=%lu", end_num, target_num);
  }
  if (end_num != target_num) {
    return -1;
  }
  ctx.stop();
}