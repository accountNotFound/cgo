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
    {
      cgo::Selector s;
      s.on("connect"_u, cgo::collect(client.connect("127.0.0.1", 8080)));
      s.on("timeout"_u, cgo::Timer(timeout_ms).chan());
      switch (co_await s.recv()) {
        case "connect"_u: {
          break;
        };
        case "timeout"_u: {
          throw cgo::SocketException(client.fileno(), "connect timeout");
        }
      }
    }
    auto req = cgo::util::format("cli{%d} send data %d", cli_id, i);
    co_await client.send(req);
    {
      cgo::Selector s;
      s.on("recv"_u, cgo::collect(client.recv(256)));
      s.on("timeout"_u, cgo::Timer(timeout_ms).chan());
      switch (co_await s.recv()) {
        case "recv"_u: {
          auto rsp = s.cast<std::string>();
          break;
        };
        case "timeout"_u: {
          throw cgo::SocketException(client.fileno(),
                                     "recv timeout, expect cli=" + std::to_string(cli_id) + ", i=" + std::to_string(i));
        }
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