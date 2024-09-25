#include "core/context.h"
#include "mtest.h"

const size_t exec_num = 4;
const size_t foo_num = 1000;
const size_t foo_loop = 1000;

TEST(schedule, yield) {
  std::atomic<int> res = 0;

  cgo::start_context(exec_num);
  for (int i = 0; i < foo_num; i++) {
    cgo::spawn([](std::atomic<int>& res) -> cgo::Coroutine<void> {
      for (int i = 0; i < foo_loop; i++) {
        co_await cgo::yield();
      }
      res.fetch_add(1);
    }(res));
  }
  while (res < foo_num) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  cgo::stop_context();
  ASSERT(res == foo_num, "");
}

TEST(schedule, mutex) {
  std::atomic<int> res = 0;
  cgo::Mutex mtx;

  cgo::start_context(exec_num);
  for (int i = 0; i < foo_num; i++) {
    cgo::spawn([](std::atomic<int>& res, cgo::Mutex& mtx) -> cgo::Coroutine<void> {
      for (int i = 0; i < foo_loop; i++) {
        cgo::LockGuard guard((co_await mtx.lock(), mtx));
        res++;
        co_await cgo::yield();
      }
    }(res, mtx));
  }
  while (res < foo_num * foo_loop) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  cgo::stop_context();
  ASSERT(res == foo_num * foo_loop, "");
}