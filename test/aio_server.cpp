#include "aio/socket.h"
#include "core/executor.h"

#define USE_DEBUG
#define USE_ASSERT
#include "util/format.h"

class CallbackGuard {
 public:
  CallbackGuard(std::function<void()>&& callback) : _callback(callback) {}
  ~CallbackGuard() { this->_callback(); }

 private:
  std::function<void()> _callback;
};

const size_t exec_num = 4;

cgo::Mutex mutex;
size_t end_num = 0;

cgo::Coroutine<void> run_server() {
  cgo::Socket server;
  CallbackGuard guard([&server]() { server.close(); });

  server.bind(8080);
  server.listen();
  printf("-----run server start\n");
  while (true) {
    auto conn = co_await server.accept();
    ASSERT(conn.fileno() > 0, "invalid fd: %d\n", conn.fileno());
    cgo::spawn([](cgo::Socket conn) -> cgo::Coroutine<void> {
      CallbackGuard guard([&conn]() { conn.close(); });

      try {
        auto req = co_await conn.recv(256);
        printf("recv '%s'\n", req.data());
        co_await conn.send("echo from server: " + req);
      } catch (const cgo::SocketException& e) {
        printf("%s\n", e.what());
      }
      co_await mutex.lock();
      end_num++;
      mutex.unlock();
    }(conn));
  }
  printf("-----run server end\n");
  co_return;
}

int main() {
  cgo::_impl::ScheduleContext ctx;
  cgo::_impl::EventHandler handler;
  cgo::_impl::TaskExecutor exec(&ctx, &handler);

  exec.start(exec_num);
  cgo::spawn(run_server());
  while (true) {
    handler.handle();
    // DEBUG("end_num=%lu, target_num=%lu\n", end_num, target_num);
  }
  exec.stop();
}