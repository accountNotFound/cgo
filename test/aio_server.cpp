#include <chrono>

#include "aio/socket.h"

// #define USE_DEBUG
#include "util/log.h"

using namespace cgo::impl;

const size_t exec_num = 4;

Async<void> run_server() {
  Socket server;
  server.bind(8080);
  server.listen();
  while (true) {
    auto conn = co_await server.accept();
    Context::current().spawn([](Socket conn) -> Async<void> {
      auto req = co_await conn.recv(100);
      printf("recv '%s'\n", req.data());
      co_await conn.send("echo from server: " + req);
      conn.close();
    }(conn));
  }
  printf("-----run server end\n");
  co_return;
}

int main() {
  Context ctx;
  ctx.start(exec_num);
  ctx.spawn(run_server());
  while (true) {
    ctx.handler().handle();
  }
  ctx.stop();
}