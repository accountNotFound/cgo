#include "core/context.h"
#include "mtest.h"

const size_t exec_num = 4;
const size_t foo_num = 1000;
const size_t foo_loop = 1000;

TEST(schedule, yield) {
  std::atomic<int> res = 0;

  cgo::Context ctx;
  ctx.start(exec_num);
  for (int i = 0; i < foo_num; i++) {
    cgo::spawn(&ctx, [](std::atomic<int>& res) -> cgo::Coroutine<void> {
      for (int i = 0; i < foo_loop; i++) {
        co_await cgo::yield();
      }
      res.fetch_add(1);
    }(res));
  }
  while (res < foo_num) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  ctx.stop();
  ASSERT(res == foo_num, "");
}

TEST(schedule, mutex) {
  std::atomic<int> res = 0;
  cgo::Mutex mtx;

  cgo::Context ctx;
  ctx.start(exec_num);
  for (int i = 0; i < foo_num; i++) {
    cgo::spawn(&ctx, [](std::atomic<int>& res, cgo::Mutex& mtx) -> cgo::Coroutine<void> {
      for (int i = 0; i < foo_loop; i++) {
        co_await mtx.lock();
        auto guard = cgo::defer([&mtx]() { mtx.unlock(); });
        res++;
        co_await cgo::yield();
      }
    }(res, mtx));
  }
  while (res < foo_num * foo_loop) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  ctx.stop();
  ASSERT(res == foo_num * foo_loop, "");
}

TEST(schedule, force_stop) {
  std::atomic<int> res = 0;
  cgo::Mutex mtx;

  cgo::Context ctx;
  ctx.start(exec_num);
  for (int i = 0; i < foo_num; ++i) {
    cgo::spawn(&ctx, [](decltype(mtx)& mtx, decltype(res)& res) -> cgo::Coroutine<void> {
      co_await mtx.lock();
      res.fetch_add(1);
    }(mtx, res));
  }
  std::this_thread::sleep_for(std::chrono::seconds(1));
  ctx.stop();
  ASSERT(res == 1, "");
}

TEST(schedule, mulit_context) {
  std::atomic<int> res[exec_num] = {0};
  cgo::Mutex mtx;

  cgo::Context ctxs[exec_num];
  for (int i = 0; i < exec_num; ++i) {
    ctxs[i].start(1);
  }

  auto sum = [&res]() {
    int s = 0;
    for (int i = 0; i < exec_num; ++i) {
      s += res[i];
    }
    return s;
  };

  auto func = [](decltype(mtx)& mtx, decltype(res[0])& res) -> cgo::Coroutine<void> {
    for (int i = 0; i < foo_loop; ++i) {
      auto tid1 = std::this_thread::get_id();
      co_await mtx.lock();
      auto tid2 = std::this_thread::get_id();
      ASSERT(tid1 == tid2, "");
      res++;
      mtx.unlock();
    }
  };

  for (int i = 0; i < foo_num; ++i) {
    cgo::spawn(&ctxs[i % exec_num], func(mtx, res[i % exec_num]));
  }

  while (sum() < foo_num * foo_loop) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  for (int j = 0; j < exec_num; ++j) {
    ctxs[j].stop();
  }
}