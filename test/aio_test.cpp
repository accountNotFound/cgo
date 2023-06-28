#include <chrono>

#include "aio/atime.h"
#include "aio/net.h"
#include "core/channel.h"

// #define USE_DEBUG
#include "util/log.h"

using namespace cgo::impl;

namespace time_test {

const size_t exec_num = 10;
size_t foo_num = 3000, foo_loop = 100;
size_t end_num = 0;

constexpr long sleep_millisec = 50;

Mutex lock;

Async<void> foo(std::string name) {
  long ts = 0;
  DEBUG("foo {%s} begin", name.data());
  for (int i = 0; i < foo_loop; ++i) {
    co_await cgo::impl::sleep(sleep_millisec + i);
    ts += sleep_millisec + i;
    // DEBUG("foo {%s} at {%u} millisec", name.data(), ts);
  }
  DEBUG("foo {%s} end", name.data());
  co_await lock.lock();
  end_num++;
  DEBUG("end_um=%d", end_num);
  lock.unlock();
}

std::chrono::milliseconds current_millisec() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch());
}

int test() {
  Context ctx;
  ctx.start(exec_num);
  auto begin_ms = current_millisec();
  for (int i = 0; i < foo_num; i++) {
    ctx.spawn(foo("foo_" + std::to_string(i)));
  }
  while (end_num < foo_num) {
    ctx.handler().handle();
  }
  ctx.stop();
  auto end_ms = current_millisec();
  size_t theory_ms_cost = (sleep_millisec + sleep_millisec + foo_loop) * foo_loop / 2;
  printf("theory time cost: %lu\n", theory_ms_cost);
  printf("real time cost: %lu\n", (end_ms - begin_ms).count());
  if (end_ms - begin_ms > std::chrono::milliseconds(theory_ms_cost * 2) ||
      end_ms - begin_ms < std::chrono::microseconds(theory_ms_cost)) {
    return -1;
  }
  return 0;
}

}  // namespace time_test

namespace net_test {

const size_t exec_num = 10;

const size_t server_port = 8080;

size_t client_num = 10, connection_num = 100;

Mutex mtx;
size_t client_end = 0;

Async<void> run_server() {
  TcpServer server(server_port);
  printf("server start on %lu\n", server_port);
  while (true) {
    auto conn = co_await server.accept();
    Context::current().spawn([](Connection conn) -> Async<void> {
      auto req = co_await conn.recv();
      printf("recv from client: \"%s\"\n", req.data());
      co_await conn.send("echo from server: " + req);
      printf("connection close\n");
    }(conn));
  }
}

Async<void> run_client(int cli_id) {
  co_await cgo::impl::sleep(500);  // wait server start
  std::string req = "req from client ";
  TcpClient client("0.0.0.0", server_port);
  printf("tcp client created\n");
  for (int i = 0; i < connection_num; i++) {
    auto conn = co_await client.connect();
    co_await conn.send(req + std::to_string(cli_id) + ", data: " + std::to_string(i));
    // printf("send \"%s%d\" to server\n", req.data(), i);
    auto rsp = co_await conn.recv();
    printf("recv from server: \"%s\"\n", rsp.data());
  }
  co_await mtx.lock();
  client_end++;
  mtx.unlock();
}

int test() {
  Context ctx;
  ctx.start(exec_num);
  // ctx.spawn(run_server());
  for (int i = 0; i < client_num; i++) {
    ctx.spawn(run_client(i));
  }
  while (client_end < client_num) {
    Context::current().handler().handle(128, 0);
  }
  return 0;
}

}  // namespace net_test

int main() {
  // int code = time_test::test();
  // return code;
  return net_test::test();
}