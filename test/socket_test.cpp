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

        // select an activated channel and do corresponding actions. Here is a simple recv with timeout example
        auto recv_chan = cgo::collect(conn.recv(256));
        cgo::Timer recv_timer(10 * 1000);
        auto recv_timeout = recv_timer.chan();
        for (cgo::Selector s;; co_await s.wait()) {
          if (s.test(recv_chan)) {
            auto req = s.cast<std::string>();
            co_await conn.send("echo from server: " + req);
            break;
          } else if (s.test(recv_timeout)) {
            printf("in server: conn{%d} recv timeout after 10 sec\n", conn.fileno());
            break;
          }
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

      // connect with timeout
      auto conn_chan = cgo::collect(conn.connect("127.0.0.1", 8080));
      cgo::Timer conn_timer(10 * 1000);
      auto conn_timeout = conn_timer.chan();
      bool connect_flag = false;
      for (cgo::Selector s;; co_await s.wait()) {
        if (s.test(conn_chan)) {
          connect_flag = true;
          break;
        } else if (s.test(conn_timeout)) {
          printf("in client[%d]: conn{%d} connect timeout after 10 sec\n", cli_id, conn.fileno());
          break;
        }
      }
      if (!connect_flag) {
        continue;
      }
      auto req = cgo::util::format("cli{%d} send data %d", cli_id, i);
      co_await conn.send(req);

      // recv with timeout
      auto recv_chan = cgo::collect(conn.recv(256));
      cgo::Timer recv_timer(10 * 1000);
      auto recv_timeout = recv_timer.chan();
      for (cgo::Selector s;; co_await s.wait()) {
        if (s.test(recv_chan)) {
          auto rsp = s.cast<std::string>();
          co_await mtx.lock();
          end_num++;
          mtx.unlock();
          i++;
          break;
        } else if (s.test(recv_timeout)) {
          printf("in client[%d]: conn{%d} connect timeout after 10 sec\n", cli_id, conn.fileno());
          break;
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