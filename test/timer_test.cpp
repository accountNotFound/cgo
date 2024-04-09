#include "aio/atime.h"
#include "core/executor.h"

const size_t exec_num = 4;
const size_t foo_num = 10000;
const size_t foo_loop = 100;

cgo::Mutex mutex;
size_t end_num = 0;

cgo::Coroutine<void> foo(std::string name) {
  for (int i = 0; i < foo_loop; i++) {
    co_await cgo::sleep(100);
  }
  co_await mutex.lock();
  end_num++;
  mutex.unlock();
}

int main() {
  cgo::_impl::ScheduleContext ctx;
  cgo::_impl::EventHandler handler;
  cgo::_impl::TaskExecutor exec(&ctx, &handler);

  exec.start(exec_num);
  for (int i = 0; i < foo_num; i++) {
    std::string name = "foo_" + std::to_string(i);
    cgo::spawn(foo(name));
  }

  size_t target_num = foo_num;
  while (end_num < target_num) {
    handler.handle();
  }
  if (end_num != target_num) {
    return -1;
  }
  exec.stop();
}