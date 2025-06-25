#include "core/timer.h"

#include "core/channel.h"
#include "core/context.h"
#include "mtest.h"

const size_t exec_num = 4;
const size_t foo_num = 10000;
const size_t foo_loop = 100;

cgo::Mutex mtx;
std::atomic<size_t> end_num = 0;

cgo::Coroutine<void> foo(std::chrono::milliseconds wait_ms) {
  auto begin = std::chrono::steady_clock::now();
  for (int i = 0; i < foo_loop; ++i) {
    co_await cgo::sleep(wait_ms);
  }
  auto end = std::chrono::steady_clock::now();
  auto time_cost = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin);

  // assertion just for performance
  ASSERT(time_cost < wait_ms * foo_loop * 1.5, "timeout: actual=%lums, expect=%lums\n", time_cost.count(),
         wait_ms.count() * foo_loop);

  co_await mtx.lock();
  auto guard = cgo::defer([]() { mtx.unlock(); });
  end_num.fetch_add(1);
}

TEST(timer, sleep) {
  auto begin = std::chrono::steady_clock::now();

  cgo::start_context(exec_num);
  for (int i = 0; i < foo_num; ++i) {
    int r = std::rand() % 100 + 1;
    auto ms = std::chrono::milliseconds(r);
    cgo::spawn(foo(ms));
  }

  int prev_end_num = 0;
  while (end_num < foo_num) {
    if ((float)(end_num - prev_end_num) > 0.1 * foo_num) {
      ::printf("progress: %lu/%lu\n", end_num.load(), foo_num);
      prev_end_num = end_num;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  cgo::stop_context();

  auto end = std::chrono::steady_clock::now();
  auto time_cost = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin);

  // assertion just for performance
  ASSERT(time_cost.count() < 100 * foo_loop * 1.5, "");
}