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
  cgo::spawn(
    cgo::this_coroutine_ctx(), 
    [](int timeout_ms) -> cgo::Coroutine<void> {
      co_await cgo::sleep(cgo::this_coroutine_ctx(), std::chrono::milliseconds(timeout_ms));
    }(timeout_ms));
}

void foo() {
  cgo::Context ctx;
  ctx.start(/*thread_num=*/1)
  cgo::spawn(ctx, bar());
  // ...
  ctx.stop();
}
```
3. channel and select
```c++
cgo::Coroutine<void> select() {
  cgo::Channel<int> ichan;
  cgo::Channel<std::string> schan;

  cgo::spawn(
    cgo::this_coroutine_ctx(), 
    [](cgo::Channel<std::string> chan) -> cgo::Coroutine<void> {
      cgo::sleep(cgo::this_coroutine_ctx(), std::chrono::milliseconds(1000));
      co_await (chan << "123");
    }(schan));

  {
    cgo::Select sel;
    sel.on(1, ichan) << 100;

    std::string str;
    sel.on(2, schan) >> str;

    sel.on(3, cgo::collect(cgo::sleep(std::chrono::milliseconds(5000)))) >> cgo::Dropout{}; // discard the value

    sel.on_default(); // enable a default case (-1)

    switch(co_await sel()) {
      case 1: {
        // never be here. Because ichan has no buffer and no reciever (`sel` is a sender in this case)
      }
      case 2: {
        // go here after 1 sec if `Select::on_default()` is not called
        // or it maybe ignored with `sel.on_default()`
      }
      case 3: {
        // nerver be here. Because this case is activated later (5 sec) than case 2 (1 sec)
      }
      default: {
        // a.k.a the case -1. see `Select::on_default()`
        // go here immediately if `Select::on_default()` is called and no other case becomes activated
      }
    }
  }
}
```
4. async socket
```c++
cgo::Coroutine<void> server() {
  cgo::Socket server = cgo::Socket::create(cgo::this_coroutine_ctx());
  cgo::DeferGuard guard = defer([&server]() { server.close(); });

  server.bind(8080);
  server.listen(1000);  

  cgo::Socket conn = co_await server.accept();

  cgo::spawn([](cgo::Socket conn)->cgo::Coroutine<void> {
    cgo::Defer defer([&conn]() { conn.close(); });

    auto req = co_await conn.recv(256, /*timeout=*/std::chrono::milliseconds(5000));
    if(!req) {
      // error process
      std::string msg = req.error().msg;
    }
    co_await conn.send("echo from server: " + *req);
  }(conn));

}
```
You can see more detail about this in `test/unit_test/socket_test.cpp`
