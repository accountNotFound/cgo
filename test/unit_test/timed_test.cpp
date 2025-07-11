#include "core/timed.h"

#include "core/channel.h"
#include "core/context.h"
#include "mtest.h"

const size_t exec_num = 4;
const size_t foo_num = 10000;
const size_t foo_loop = 100;

cgo::Coroutine<void> foo(int fid, std::chrono::milliseconds wait_ms, std::atomic<size_t>& end_num) {
  auto& ctx = cgo::this_coroutine_ctx();
  auto begin = std::chrono::steady_clock::now();
  for (int i = 0; i < foo_loop; ++i) {
    int r = std::rand() % 3;
    switch (r) {
      case 0: {
        co_await cgo::sleep(ctx, wait_ms);
        break;
      }
      case 1: {
        co_await (cgo::timeout(ctx, wait_ms) >> cgo::Dropout{});
        break;
      }
      case 2: {
        cgo::Select select;
        select.on(1, cgo::collect(ctx, cgo::sleep(ctx, wait_ms))) >> cgo::Dropout{};
        co_await select();
      }
    }
  }

  auto end = std::chrono::steady_clock::now();
  auto time_cost = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin);

  // assertion just for performance
  ASSERT(time_cost < wait_ms * foo_loop * 1.5, "timeout: actual=%lums, expect=%lums\n", time_cost.count(),
         wait_ms.count() * foo_loop);

  end_num.fetch_add(1);
}

TEST(timer, sleep) {
  auto begin = std::chrono::steady_clock::now();

  std::atomic<size_t> end_num = 0;
  cgo::Context ctx;
  ctx.start(exec_num);
  for (int i = 0; i < foo_num; ++i) {
    int r = std::rand() % 100 + 1;
    auto ms = std::chrono::milliseconds(r);
    cgo::spawn(ctx, foo(i, ms, end_num));
  }

  auto prev_check_ts = std::chrono::steady_clock::now();
  while (end_num < foo_num) {
    auto now = std::chrono::steady_clock::now();
    if (now - prev_check_ts > std::chrono::seconds(1)) {
      ::printf("progress: %lu/%lu\n", end_num.load(), foo_num);
      prev_check_ts = now;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  ctx.stop();

  auto end = std::chrono::steady_clock::now();
  auto time_cost = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin);

  // assertion just for performance
  ASSERT(time_cost.count() < 100 * foo_loop * 1.5, "");
}

TEST(timed, ctx_stop) {
  std::atomic<size_t> end_num = 0;
  cgo::Context ctx;
  ctx.start(exec_num);
  for (int i = 0; i < foo_num; ++i) {
    int r = std::rand() % 100 + 1;
    cgo::spawn(ctx, foo(i, std::chrono::milliseconds(r), end_num));
  }
  auto prev_check_ts = std::chrono::steady_clock::now();
  while (end_num < foo_num / 2) {
    auto now = std::chrono::steady_clock::now();
    if (now - prev_check_ts > std::chrono::seconds(1)) {
      ::printf("progress: %lu/%lu\n", end_num.load(), foo_num);
      prev_check_ts = now;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  ctx.stop();
}