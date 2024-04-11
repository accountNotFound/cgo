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
  server.listen(1000);  // give a smaller backlog and you'll see timeout log printed in this test
  while (true) {
    try {
      cgo::Socket c = co_await server.accept();

      // go a new coroutine to serve this connnection
      cgo::spawn([](cgo::Socket conn) -> cgo::Coroutine<void> {
        cgo::Defer defer([&conn]() { conn.close(); });

        try {
          cgo::Selector s;
          s.on("recv"_u, cgo::collect(conn.recv(256)));
          s.on("timeout"_u, cgo::Timer(10 * 1000).chan());
          switch (co_await s.recv()) {
            case "recv"_u: {
              auto req = s.cast<std::string>();
              co_await conn.send("echo from server: " + req);
              break;
            };
            case "timeout"_u: {
              printf("in server: conn{%d} recv timeout after 10 sec\n", conn.fileno());
              break;
            }
          }
        } catch (const cgo::SocketException& e) {
          printf("in server: %s\n", e.what());
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
      {
        cgo::Selector s;
        s.on("connect"_u, cgo::collect(conn.connect("127.0.0.1", 8080)));
        s.on("timeout"_u, cgo::Timer(10 * 1000).chan());
        switch (co_await s.recv()) {
          case "connect"_u: {
            break;
          };
          case "timeout"_u: {
            printf("in client[%d]: conn{%d} connect timeout after 10 sec\n", cli_id, conn.fileno());
            continue;
          }
        }
      }
      auto req = cgo::util::format("cli{%d} send data %d", cli_id, i);
      co_await conn.send(req);
      {
        cgo::Selector s;
        s.on("recv"_u, cgo::collect(conn.recv(256)));
        s.on("timeout"_u, cgo::Timer(10 * 1000).chan());
        switch (co_await s.recv()) {
          case "recv"_u: {
            auto rsp = s.cast<std::string>();
            co_await mtx.lock();
            end_num++;
            mtx.unlock();
            i++;
            break;
          };
          case "timeout"_u: {
            printf("in client[%d]: conn{%d} recv timeout after 10 sec\n", cli_id, conn.fileno());
            continue;
          }
        }
      }
    } catch (const cgo::SocketException& e) {
      ok = false;
      printf("in client[%d]: %s\n", cli_id, e.what());
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