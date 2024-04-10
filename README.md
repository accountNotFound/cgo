# build and test
```bash
mkdir build; cd build

cmake ..; make

ctest -C test
```

# library features
1. coroutine nested call
```c++
cgo::Coroutine<int> bar() {
  co_return 123;
}

cgo::Coroutine<std::string> foo() {
  int i = co_await bar(); // wait bar() finish and get returned value
  co_return std::to_string(i);
}
```
2. spawn, yield and sleep operations
```c++
cgo::Coroutine<void> bar() {
  co_await cgo::yield();
  int timeout_ms = 1000;

  // do not use lambda capture. See notes in cppreference
  cgo::spawn([](int timeout_ms) -> cgo::Coroutine<void> {
    co_await cgo::sleep(timeout_ms);
  }(timeout_ms));
}

void foo() {
  cgo::spawn(bar());
}
```
3. channel and select
```c++
cgo::Coroutine<void> select_tset() {
  cgo::Channel<int> ich; 
  cgo::Channel<std::string> sch; 

  cgo::spawn([](cgo::Channel<int> ich) -> cgo::Coroutine<void> {
    int v = co_await ich.recv();
    co_await ich.send(std::move(v));
  }(ich));

  cgo::spawn([](cgo::Channel<std::string> sch) -> cgo::Coroutine<void> {
    co_await cgo::sleep(1000);
    co_await sch.send("123");
  }(sch));

  // when any of channel converts to readable, selector will be notified and test certain channel again
  for (cgo::Selector s;; co_await s.wait()) {
    if (s.test(ich)) {
      // this channel is always empty, so this block is unreachable
      int num = s.cast<int>();
      break;
    } else if (s.test(sch)) {
      // readable after 1 second, cast to get string value from channel 
      auto str = s.cast<std::string>();
      break;
    }
  }
}
```
4. async socket
```c++
cgo::Coroutine<void> server() {
  cgo::Socket server;
  cgo::Defer defer([&server]() { server.close(); });

  server.bind(8080);
  server.listen(1000);  
  cgo::Socket conn = co_await server.accept();
  std::string req = co_await conn.recv(256);
  co_await conn.send("service end");
}
```
You can see more detail about this in `test/socket_test.cpp`
