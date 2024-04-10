#include "aio/atime.h"
#include "aio/socket.h"
#include "core/executor.h"
#include "util/format.h"

class CallbackGuard {
 public:
  CallbackGuard(std::function<void()>&& callback) : _callback(callback) {}
  ~CallbackGuard() { this->_callback(); }

 private:
  std::function<void()> _callback;
};

const size_t exec_num = 4;
const size_t cli_num = 1000, conn_num = 100;

cgo::Mutex mtx;
int end_num = 0;

cgo::Coroutine<void> run_client(int cli_id) {
  const size_t timeout_ms = 10 * 1000;
  for (int i = 0; i < conn_num; i++) {
    cgo::Socket client;
    CallbackGuard guard([&client]() { client.close(); });

    auto conn_chan = cgo::collect(client.connect("127.0.0.1", 8080));
    cgo::Timer conn_timer(timeout_ms);
    auto conn_timeout = conn_timer.chan();
    for (cgo::Selector s;; co_await s.wait()) {
      if (s.test(conn_chan)) {
        break;
      } else if (s.test(conn_timeout)) {
        throw cgo::SocketException(client.fileno(), "connect timeout");
      }
    }

    auto req = cgo::util::format("cli{%d} send data %d", cli_id, i);
    co_await client.send(req);

    auto recv_chan = cgo::collect(client.recv(256));
    cgo::Timer recv_timer(timeout_ms);
    auto recv_timeout = recv_timer.chan();
    for (cgo::Selector s;; co_await s.wait()) {
      if (s.test(recv_chan)) {
        auto rsp = s.cast<std::string>();
        break;
      } else if (s.test(recv_timeout)) {
        throw cgo::SocketException(client.fileno(),
                                   "recv timeout, expect cli=" + std::to_string(cli_id) + ", i=" + std::to_string(i));
      }
    }
  }
  printf("cli{%d} end\n", cli_id);
  co_await mtx.lock();
  end_num++;
  mtx.unlock();
}

int main() {
  cgo::_impl::ScheduleContext ctx;
  cgo::_impl::EventHandler handler;
  cgo::_impl::TaskExecutor exec(&ctx, &handler);

  exec.start(exec_num);
  for (int i = 0; i < cli_num; i++) {
    cgo::spawn(run_client(i));
  }
  while (end_num < cli_num) {
    handler.handle();
  }
  exec.stop();
}