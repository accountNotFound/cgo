#include <chrono>

#include "aio/socket.h"

// #define USE_DEBUG
#include "util/format.h"
#include "util/log.h"

using namespace cgo::impl;

const size_t exec_num = 4;
const size_t cli_num = 10, conn_num = 10;

Mutex mtx;
int end_num = 0;

Async<void> run_client(int cli_id) {
  for (int i = 0; i < conn_num; i++) {
    Socket client;
    co_await client.connect("127.0.0.1", 8080);
    printf("cli{%d} [%d] fd=%d connected\n", cli_id, i, client.fileno());
    auto req = format("cli{%d} send data %d", cli_id, i);
    co_await client.send(req);
    auto rsp = co_await client.recv(100);
    printf("%s\n", rsp.data());
    client.close();
    printf("cli{%d} [%d] fd=%d closed\n", cli_id, i, client.fileno());
  }
  printf("cli{%d} end\n", cli_id);
  co_await mtx.lock();
  end_num++;
  mtx.unlock();
}

int main() {
  Context ctx;
  ctx.start(exec_num);
  for (int i = 0; i < cli_num; i++) {
    ctx.spawn(run_client(i));
  }
  while (end_num < cli_num) {
    ctx.handler().handle();
  }
  ctx.stop();
}