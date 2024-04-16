#include <chrono>

#include "cgo.h"

const size_t exec_num = 4;
const size_t cli_num = 1000, conn_num = 100;

cgo::Mutex mtx;
int end_num = 0;  // test end flag

cgo::Coroutine<void> run_server() {
  cgo::Socket server;
  cgo::Defer defer([&server]() { server.close(); });

  server.bind(8080);
  server.listen(1024);  // give a smaller backlog and you'll see timeout log printed in this test
  while (true) {
    try {
      cgo::Socket c = co_await server.accept();

      // go a new coroutine to serve this connnection
      cgo::spawn([](cgo::Socket conn) -> cgo::Coroutine<void> {
        cgo::Defer defer([&conn]() { conn.close(); });

        try {
          auto req = co_await cgo::timeout(conn.recv(256), 10 * 1000);
          co_await conn.send("echo from server: " + req);
        } catch (const cgo::SocketException& e) {
          printf("in server socket err: %s\n", e.what());
        } catch (const cgo::TimeoutException& e) {
          printf("in server timeout err: %s\n", e.what());
        }
      }(c));

    } catch (const cgo::SocketException& e) {
      printf("in server: %s\n", e.what());
    }
  }
}

cgo::Coroutine<void> run_client(int cli_id) {
  for (int i = 0; i < conn_num;) {
    bool ok = true;
    try {
      cgo::Socket conn;
      cgo::Defer defer([&conn]() { conn.close(); });

      co_await cgo::timeout(conn.connect("127.0.0.1", 8080), 10 * 1000);
      co_await conn.send(cgo::util::format("cli{%d} send data %d", cli_id, i));
      auto rsp = co_await cgo::timeout(conn.recv(256), 10 * 1000);
      co_await mtx.lock();
      end_num++;
      i++;
      mtx.unlock();
    } catch (const cgo::SocketException& e) {
      ok = false;
      printf("in client[%d] socket err: %s\n", cli_id, e.what());
    } catch (const cgo::TimeoutException& e) {
      ok = false;
      printf("in client[%d] timeout err: %s\n", cli_id, e.what());
    }
    if (!ok) {
      co_await cgo::sleep(1000);
    }
  }
}

int main() {
  cgo::Context ctx;
  ctx.start(exec_num);

  cgo::spawn(run_server());
  std::this_thread::sleep_for(std::chrono::milliseconds(50));  // wait server running

  for (int i = 0; i < cli_num; i++) {
    cgo::spawn(run_client(i));
  }

  int prev_end_num = 0;
  ctx.loop([&prev_end_num]() {
    int target_num = cli_num * conn_num;
    if ((end_num - prev_end_num) * 10 > target_num) {
      printf("progress: %d/%d\n", end_num, target_num);
      prev_end_num = end_num;
    }
    return end_num < target_num;
  });
  ctx.stop();
}