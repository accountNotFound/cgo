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
cgo::Coroutine<void> select() {
  cgo::Channel<int> ichan;
  cgo::Channel<std::string> schan;

  cgo::spawn([](cgo::Channel<int> ichan, cgo::Channel<std::string> chan) -> cgo::Coroutine<void> {
    cgo::sleep(1000);
    co_await schan.send("123");
    int v = co_await ichan.recv(); // block here forever
  }(ichan, schan));

  {
    // suggest to destroy selector immediately when case-switch done
    // the Selector::on() only support int type as key, but you can write following code by User-Defined-Literal
    cgo::Selector s;
    s.on("empty_ichan"_u, ichan)
     .on("schan_after_1_sec"_u, schan);
    switch ((co_await s.recv())) {
      case "empty_ichan"_u: {
        // never be here
        break;
      }
      case "schan_after_1_sec"_u: {
        auto str = s.cast<std::string>(); // selector recv value with any type, cast it
        break;
      }
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

  int64_t recv_timeout_ms = 10*1000;
  try {
    std::string req = co_await cgo::timeout(conn.recv(256), recv_timeout_ms);
  } catch (const cgo::TimeoutException& e) {
    printf("recv timeout\n");
    co_return;
  }
  co_await conn.send("service end");
}
```
You can see more detail about this in `test/socket_test.cpp`
