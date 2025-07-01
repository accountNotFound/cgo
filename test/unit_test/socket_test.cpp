#include "core/channel.h"
#include "core/context.h"
#include "core/event.h"
#include "core/timer.h"
#include "mtest.h"

const size_t exec_num = 4;
const size_t cli_num = 1000, conn_num = 100;
std::atomic<size_t> end_cli_num = 0;
bool end_svr_flag = false;

const size_t port = 8080;
const size_t back_log = 1024;  // give a smaller backlog and you'll see timeout log printed in this test

template <typename... Args>
std::string format(const char* fmt, Args... args) {
  constexpr size_t oldlen = 512;
  std::string buffer(oldlen, '\0');

  size_t newlen = snprintf(buffer.data(), oldlen, fmt, args...);
  newlen++;

  if (newlen > oldlen) {
    std::string newbuffer(newlen, '\0');
    snprintf(newbuffer.data(), newlen, fmt, args...);
    return newbuffer;
  }
  buffer.resize(newlen);
  return buffer;
}

template <typename V, typename E>
V get_or_raise(const std::expected<V, E>& ex) {
  if (!ex.has_value()) {
    throw ex.error();
  }
  if constexpr (!std::is_void_v<V>) {
    return std::move(ex.value());
  }
}

cgo::Coroutine<bool> send_request(int cli, int data) {
  cgo::Socket s;
  auto guard = cgo::defer([&s]() { s.close(); });
  try {
    auto timeout = std::chrono::seconds(10);
    get_or_raise(co_await s.connect("127.0.0.1", port, timeout));
    get_or_raise(co_await s.send(format("{cli=%d} send {data=%d}", cli, data), timeout));
    auto rsp = get_or_raise(co_await s.recv(256, timeout));
    co_return true;
  } catch (const cgo::Socket::Error& e) {
    ::printf("{cli=%d, data=%d} request error: %s\n", cli, data, e.msg.data());
    co_return false;
  }
}

cgo::Coroutine<void> handle_request(cgo::Socket conn) {
  auto guard = cgo::defer([&conn]() { conn.close(); });
  try {
    auto timeout = std::chrono::seconds(5);
    auto req = get_or_raise(co_await conn.recv(256, timeout));
    get_or_raise(co_await conn.send(format("server echo: '%s'\n", req.data())));
  } catch (const cgo::Socket::Error& e) {
    ::printf("server: {fd=%d} handle error: %s\n", int(conn), e.msg.data());
  }
}

cgo::Coroutine<void> run_client(int cli) {
  int i = 0;
  while (i < conn_num) {
    bool ok = co_await send_request(cli, i);
    if (ok) {
      i++;
    }
  }
  end_cli_num.fetch_add(1);
}

cgo::Coroutine<void> run_server() {
  cgo::Socket sock;
  sock.bind(port);
  sock.listen(back_log);
  while (!end_svr_flag) {
    auto conn = co_await sock.accept();
    if (!conn.has_value()) {
      continue;
    }
    cgo::spawn(handle_request(conn.value()));
  }
}

TEST(socket, simple) {
  cgo::start_context(exec_num);
  cgo::spawn(run_server());
  for (int i = 0; i < cli_num; ++i) {
    cgo::spawn(run_client(i));
  }
  auto prev_check_tp = std::chrono::steady_clock::now();
  while (end_cli_num < cli_num) {
    auto now = std::chrono::steady_clock::now();
    if (now - prev_check_tp > std::chrono::seconds(1)) {
      ::printf("progress: %lu/%lu\n", end_cli_num.load(), cli_num);
      prev_check_tp = now;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  end_svr_flag = true;
  cgo::stop_context();
}