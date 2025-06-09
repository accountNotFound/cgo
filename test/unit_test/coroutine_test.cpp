#include "core/coroutine.h"

#include <any>

#include "mtest.h"

const int bar_throw_threshold = 5;
int suspend_cnt = 0;

cgo::Coroutine<int> bar(int n) {
  int res = 0;
  for (int i = 0; i < n; i++) {
    if (i >= bar_throw_threshold) {
      throw i;
    }
    res += i;
    // printf("   bar\n");
    co_await std::suspend_always{};
    suspend_cnt++;
  }
  co_return std::move(res);
}

cgo::Coroutine<std::any> foo(int n) {
  // printf("  foo\n");
  int res = co_await bar(n);
  co_return std::make_any<std::string>("return from foo: " + std::to_string(res));
}

cgo::Coroutine<void> biz(int n) {
  // printf(" biz\n");
  std::any res = co_await foo(n);
  std::string s = std::any_cast<std::string>(std::move(res));
  // printf("biz get: %s\n", s.data());
}

TEST(coroutine, suspend) {
  suspend_cnt = 0;
  auto f = biz(bar_throw_threshold);
  f.init();
  for (int i = 0; !f.done(); i++) {
    // printf("main\n");
    f.resume();
    ASSERT(suspend_cnt == i, "suspend failed");
  }
}

TEST(coroutine, catch_exception) {
  suspend_cnt = 0;
  auto f = biz(bar_throw_threshold * 2);
  f.init();
  for (int i = 0; !f.done(); i++) {
    // printf("main\n");
    if (i >= bar_throw_threshold) {
      ASSERT_RAISE(f.resume(), int, "catch exception failed");
    } else {
      f.resume();
    }
    ASSERT(suspend_cnt == i, "suspend failed");
  }
}

cgo::Coroutine<int> count(int n) {
  if (n == 0) {
    co_return 0;
  }
  co_return (co_await count(n - 1)) + 1;
}

TEST(coroutine, fake_recursion) {
  int num = 1e6;
  auto f = count(num);
  f.init();
  f.resume();
  ASSERT(f.await_resume() == num, "fake recursion failed");
}
