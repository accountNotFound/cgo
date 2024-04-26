#include <stdio.h>

#include <any>
#include <string>

#include "cgo.h"

cgo::Coroutine<int> bar(int n) {
  int res = 0;
  for (int i = 0; i < n; i++) {
    if (i > 5) {
      printf("  bar throw exception\n");
      throw n;
    }
    printf("   bar %d\n", i);
    co_await std::suspend_always{};
    res += i + 1;
  }
  co_return std::move(res);
}

cgo::Coroutine<std::any> foo(int n) {
  bool has_err = false;
  std::string res = "";
  for (int i = 0; i < n; i++) {
    printf("  foo %d\n", i);
    try {
      res += std::to_string(co_await bar(i)) + " ";
    } catch (int n) {
      printf(" foo catch err from bar: '%d'\n", n);
      has_err = true;
    }
  }
  if (has_err) {
    printf(" foo rethrow exception\n");
    throw "foo rethrow";
  }
  co_return std::move(res);
}

cgo::Coroutine<void> biz() {
  printf("biz start\n");
  auto res = co_await foo(10);
  printf("res type=<%s>\n", res.type().name());
  printf("\"%s\"\n", std::any_cast<std::string>(res).data());
  printf("biz end\n");
}

int main() {
  auto e = biz();
  printf("coroutine test start\n");
  try {
    for (e.start(); !e.done(); e.resume()) {
      printf("main\n");
    }
  } catch (const char* e) {
    printf("main catch: '%s'\n", e);
  }
  printf("coroutine test end\n");
}