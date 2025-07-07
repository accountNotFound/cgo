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
    cgo::spawn(ctx, [](std::atomic<int>& res) -> cgo::Coroutine<void> {
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
    cgo::spawn(ctx, [](std::atomic<int>& res, cgo::Mutex& mtx) -> cgo::Coroutine<void> {
      for (int i = 0; i < foo_loop; i++) {
        co_await mtx.lock();
        auto guard = cgo::defer([&mtx]() { mtx.unlock(); });
        res++;
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
  bool done = false;

  cgo::Context ctx;
  ctx.start(exec_num);
  for (int i = 0; i < foo_num; ++i) {
    cgo::spawn(ctx, [](decltype(mtx)& mtx, decltype(res)& res, decltype(done)& done) -> cgo::Coroutine<void> {
      co_await mtx.lock();
      res.fetch_add(1);
      done = true;
    }(mtx, res, done));
  }
  while (!done) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
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
    cgo::spawn(ctxs[i % exec_num], func(mtx, res[i % exec_num]));
  }

  while (sum() < foo_num * foo_loop) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  for (int j = 0; j < exec_num; ++j) {
    ctxs[j].stop();
  }
}

TEST(schedule, multi_ctx_force_stop) {
  std::atomic<int> res1 = 0, res2 = 0;
  cgo::Mutex mtx;
  bool done1 = false, done2 = false;

  auto func = [](decltype(mtx)& mtx, decltype(res1)& res, int stop_at, bool& done) -> cgo::Coroutine<void> {
    co_await mtx.lock();
    auto guard = cgo::defer([&mtx]() { mtx.unlock(); });
    if (done) {
      ASSERT(false, "nerver be here\n");
    }
    res++;
    if (res >= stop_at) {
      done = true;
      cgo::Semaphore sem(0);
      co_await sem.aquire();
      // mutex will never be unlock until context is stopped (destroy the coroutine and call guard defer)
    }
  };

  cgo::Context ctx1, ctx2;
  ctx1.start(exec_num);
  ctx2.start(2);

  int target1 = foo_num * 0.2;
  int target2 = foo_num - target1;
  for (int i = 0; i < foo_num; ++i) {
    cgo::spawn(ctx1, func(mtx, res1, target1, done1));
    cgo::spawn(ctx2, func(mtx, res2, target2, done2));
  }
  while (res1 < target1 || res2 < target2) {
    if (done1 && !ctx1.closed()) {
      ctx1.stop();
    }
    if (done2 && !ctx2.closed()) {
      ctx2.stop();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  ctx1.stop();
  ctx2.stop();
}