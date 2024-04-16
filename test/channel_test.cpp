#include <chrono>
#include <string>

#include "cgo.h"

// #define USE_DEBUG
#include "util/format.h"

const size_t exec_num = 4;
const size_t foo_num = 1000;
const size_t foo_loop = 1000;

cgo::Mutex mutex;
size_t end_num = 0;

cgo::Coroutine<void> foo(std::string name) {
  cgo::Channel<std::string> schan(10);
  cgo::Channel<int> ichan;

  for (int i = 0; i < foo_loop; i++) {
    cgo::spawn([](cgo::Channel<std::string> in, cgo::Channel<int> out) -> cgo::Coroutine<void> {
      std::string s = co_await in.recv();
      // DEBUG("recv s=%s\n", s.data());
      co_await out.send(1);
      // DEBUG("send s=%s\n", s.data());
    }(schan, ichan));

    co_await schan.send(std::string(name));
  }
  for (int i = 0; i < foo_loop; i++) {
    co_await ichan.recv();
    DEBUG("%s recv %d/%lu\r", name.data(), i, foo_loop);
  }

  co_await mutex.lock();
  end_num++;
  DEBUG("%s inc, end_num=%lu\n", name.data(), end_num);
  mutex.unlock();
}

int main() {
  cgo::Context ctx;

  ctx.start(exec_num);
  for (int i = 0; i < foo_num; i++) {
    std::string name = "foo_" + std::to_string(i);
    cgo::spawn(foo(name));
  }

  size_t target_num = foo_num;
  while (end_num < target_num) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    DEBUG("end_num=%lu, target_num=%lu\r", end_num, target_num);
  }
  if (end_num != target_num) {
    return -1;
  }
  ctx.stop();
}