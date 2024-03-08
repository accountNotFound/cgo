run this repo by following commands:

```shell
mkdir build && cd build
cmake ..
make
ctest -C test
```

you can write concurrent code like this:
```c++
#include <chrono>
#include <iostream>

#include "aio/atime.h"
#include "core/channel.h"

using namespace cgo::impl;

int main() {
  int thread_cnt = 4;
  Context ctx;
  ctx.start(thread_cnt);

  Channel<int> chan(1);

  ctx.spawn([](Channel<int> chan) -> Async<void> {
    int sleep_millisce = 1000;
    co_await cgo::impl::sleep(sleep_millisec);
    co_await chan.send(12345);
  }(chan));

  ctx.spawn([](Channel<int> chan) -> Async<void> {
    int res = co_await chan.recv();
    std::cout << res << std::endl;
  }(chan));

  while (true) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
}

```
