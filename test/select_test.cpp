#include <chrono>
#include <string>

#include "cgo.h"

// #define USE_DEBUG
// #define USE_ASSERT
#include "util/format.h"

const size_t exec_num = 4;
const size_t foo_num = 10000;
const size_t foo_loop = 10;

cgo::Mutex mutex;
size_t end_num = 0;

constexpr unsigned int s2i(const char *s, std::size_t l, unsigned int h) {
  return l == 0 ? h : s2i(s + 1, l - 1, (h * 33) + static_cast<unsigned char>(*s) - 'a' + 1);
}

constexpr unsigned int operator""_u(const char *s, std::size_t l) { return s2i(s, l, 0); }

cgo::Channel<int> bar(int64_t timeout_ms) {
  cgo::Channel<int> res;
  cgo::spawn([](cgo::Channel<int> chan, int64_t timeout_ms) -> cgo::Coroutine<void> {
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
        .on("timeout"_u, bar(i < 3 ? 500 : 1500))
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
        auto v = s.cast<int>();
        DEBUG("%s select %d\n", name.data(), v);
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