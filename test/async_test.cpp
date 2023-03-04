#include "core/async.h"

#include <stdio.h>

#include <string>

using namespace cgo::impl;

Async<int> bar(int n) {
  int res = 0;
  for (int i = 0; i < n; i++) {
    printf("   bar %d\n", i);
    co_await std::suspend_always{};
    res += i + 1;
  }
  co_return std::move(res);
}

Async<std::any> foo(int n) {
  std::string res = "";
  for (int i = 0; i < n; i++) {
    printf("  foo %d\n", i);
    res += std::to_string(co_await bar(i)) + " ";
  }
  co_return std::move(res);
}

Async<void> biz() {
  printf("biz start\n");
  auto res = co_await foo(10);
  printf("res type=<%s>\n", res.type().name());
  printf("\"%s\"\n", std::any_cast<std::string>(res).data());
  printf("biz end\n");
}

int main() {
  auto e = biz();
  printf("coroutine test start\n");
  for (e.start(); !e.done(); e.resume()) {
    printf("main\n");
  }
  printf("coroutine test end\n");
}