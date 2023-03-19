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

namespace single_channel_test {

const size_t exec_num = 10;
size_t foo_num = 1000, foo_loop = 1000;
size_t end_num = 0;

// Channel<bool> lock(1);
Mutex lock;

Async<void> foo(const std::string& name) {
  DEBUG("[TH-{%u}]: coroutine(%s) start", std::this_thread::get_id(), name.data());
  for (int i = 0; i < foo_loop; i++) {
    // co_await lock.send(true);
    co_await lock.lock();
    DEBUG("[TH-{%u}]: coroutine(%s) lock", std::this_thread::get_id(), name.data());
    end_num++;
    // lock.recv_nowait();
    lock.unlock();
    DEBUG("[TH-{%u}]: coroutine(%s) unlock", std::this_thread::get_id(), name.data());
  }
  DEBUG("[TH-{%u}]: coroutine(%s) end", std::this_thread::get_id(), name.data());
}

int test() {
  Context ctx;
  ctx.start(exec_num);
  for (int i = 0; i < foo_num; i++) {
    std::string name = "foo_" + std::to_string(i);
    ctx.spawn(foo(name), name);
  }
  while (end_num < foo_num * foo_loop) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  ctx.stop();
  if (end_num != foo_num * foo_loop) {
    return -1;
  }
  return 0;
}

}  // namespace single_channel_test

namespace multi_channel_test {

const size_t exec_num = 10;
size_t foo_num = 1000, foo_loop = 1000;
size_t end_num = 0;

Context ctx;
Mutex lock;

Async<void> foo(const std::string& name) {
  DEBUG("%s start", name.data());
  Channel<std::string> outchan(foo_num);
  for (int i = 0; i < foo_loop; i++) {
    // TODO: lambda capture fail
    ctx.spawn([](Channel<std::string> outchan, int i) -> Async<void> {
      DEBUG("spawn_%d start", i);
      outchan.send_nowait("hello");
      DEBUG("spawn_%d send %s", i, "hello");
      co_return;
    }(outchan, i));
  }
  int recv_cnt = 0;
  while (recv_cnt < foo_loop) {
    co_await outchan.recv();
    recv_cnt++;
  }
  co_await lock.lock();
  end_num++;
  DEBUG("%s end, end_num=%d", name.data(), end_num);
  lock.unlock();
}

int test() {
  ctx.start(exec_num);
  for (int i = 0; i < foo_num; i++) {
    std::string name = "foo_" + std::to_string(i);
    ctx.spawn(foo(name), name);
  }
  while (end_num < foo_num) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  ctx.stop();
  if (end_num != foo_num) {
    return -1;
  }
  return 0;
}

}  // namespace multi_channel_test

int main() {
  int code = single_channel_test::test();
  if (code != 0) {
    return code;
  }
  code = multi_channel_test::test();
  if (code != 0) {
    return code;
  }
}