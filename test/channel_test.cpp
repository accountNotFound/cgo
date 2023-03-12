#include "core/channel.h"

#include <chrono>
#include <string>
#include <thread>

#include "core/async.h"
#include "core/context.h"
#include "util/spin_lock.h"

// #define USE_DEBUG
#include "util/log.h"

using namespace cgo::impl;

Context ctx;

namespace single_channel_test {

const size_t exec_num = 10;
size_t foo_num = 1000, foo_loop = 1000;
size_t end_num = 0;

Channel<bool> lock(1);

Async<void> foo(const std::string& name) {
  DEBUG("[TH-{%u}]: coroutine(%s) start", std::this_thread::get_id(), name.data());
  for (int i = 0; i < foo_loop; i++) {
    co_await lock.send(true);
    DEBUG("[TH-{%u}]: coroutine(%s) lock", std::this_thread::get_id(), name.data());
    end_num++;
    co_await lock.recv();
    DEBUG("[TH-{%u}]: coroutine(%s) unlock", std::this_thread::get_id(), name.data());
  }
  DEBUG("[TH-{%u}]: coroutine(%s) end", std::this_thread::get_id(), name.data());
}

int test() {
  ctx.initialize(exec_num);
  for (int i = 0; i < foo_num; i++) {
    std::string name = "foo_" + std::to_string(i);
    ctx.start(foo(name), name);
  }
  while (end_num < foo_num * foo_loop) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  ctx.finalize();
  if (end_num != foo_num * foo_loop) {
    return -1;
  }
  return 0;
}

}  // namespace single_channel_test

int main() {
  int code = single_channel_test::test();
  if (code != 0) {
    return code;
  }
}