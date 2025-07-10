#include "core/coroutine.h"

#include <any>

#include "mtest.h"

const int bar_throw_threshold = 5;
int suspend_cnt = 0;

void init(cgo::_impl::BaseFrame& f) { cgo::_impl::FrameOperator().init(f); }

void resume(cgo::_impl::BaseFrame& f) { cgo::_impl::FrameOperator().resume(f); }

bool done(cgo::_impl::BaseFrame& f) { return cgo::_impl::FrameOperator().done(f); }

void destroy(cgo::_impl::BaseFrame& f) { cgo::_impl::FrameOperator().destroy(f); }

cgo::Coroutine<int> bar(int n) {
  int res = 0;
  for (int i = 0; i < n; i++) {
    if (i >= bar_throw_threshold) {
      throw i;
    }
    res += i;
    // ::printff("   bar\n");
    co_await std::suspend_always{};
    suspend_cnt++;
  }
  co_return res;
}

cgo::Coroutine<std::any> foo(int n) {
  // ::printff("  foo\n");
  int res = co_await bar(n);
  co_return std::make_any<std::string>("return from foo: " + std::to_string(res));
}

cgo::Coroutine<void> biz(int n) {
  // ::printff(" biz\n");
  std::any res = co_await foo(n);
  std::string s = std::any_cast<std::string>(std::move(res));
  // ::printff("biz get: %s\n", s.data());
}

TEST(coroutine, suspend) {
  suspend_cnt = 0;
  auto f = biz(bar_throw_threshold);
  init(f);
  for (int i = 0; !done(f); i++) {
    // ::printff("main\n");
    resume(f);
    ASSERT(suspend_cnt == i, "suspend failed");
  }
}

TEST(coroutine, destroy) {
  auto f = biz(bar_throw_threshold);
  init(f);
  resume(f);
  ASSERT(!done(f), "");
  destroy(f);
}

TEST(coroutine, catch_exception) {
  suspend_cnt = 0;
  auto f = biz(bar_throw_threshold * 2);
  init(f);
  for (int i = 0; !done(f); i++) {
    // ::printff("main\n");
    if (i >= bar_throw_threshold) {
      ASSERT_RAISE(resume(f), int, "catch exception failed");
    } else {
      resume(f);
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
  init(f);
  resume(f);
  ASSERT(f.await_resume() == num, "fake recursion failed");
}

int val = 0;

cgo::Coroutine<int&> get_ref() { co_return val; }

cgo::Coroutine<void> set_ref() {
  int& v = co_await get_ref();
  ASSERT(v == 0, "");
  v = 100;
  ASSERT(val == 100, "");
}

TEST(coroutine, return_ref) {
  auto f = set_ref();
  init(f);
  while (!done(f)) {
    resume(f);
  }
}