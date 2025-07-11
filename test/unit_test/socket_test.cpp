#include "core/channel.h"
#include "core/context.h"
#include "core/event.h"
#include "core/timed.h"
#include "mtest.h"

const size_t exec_num = 4;
const size_t cli_num = 1000, conn_num = 100;

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
  if (!ex) {
    throw ex.error();
  }
  if constexpr (!std::is_void_v<V>) {
    return std::move(*ex);
  }
}

cgo::Coroutine<bool> send_request(int cli, int data) {
  auto s = cgo::Socket::create(cgo::this_coroutine_ctx(), cgo::Socket::Protocol::TCP, cgo::Socket::AddressFamily::IPv4);
  auto guard = cgo::defer([&s]() { s.close(); });
  try {
    auto timeout = std::chrono::seconds(10);
    get_or_raise(co_await s.connect("127.0.0.1", port, timeout));
    get_or_raise(co_await s.send(format("{cli=%d} send {data=%d}", cli, data), timeout));
    auto rsp = get_or_raise(co_await s.recv(256, timeout));
    co_return true;
  } catch (const cgo::Socket::Error& e) {
    // ::printf("{cli=%d, data=%d} request error: '%s'\n", cli, data, e.msg.data());
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
    ::printf("server: {fd=%d} handle error: '%s'\n", int(conn), e.err_msg.data());
  }
}

cgo::Coroutine<void> run_client(int cli, std::atomic<size_t>& end_conn_num) {
  int i = 0;
  int retry_cnt = 0;
  int max_retry_cnt = 5;
  while (i < conn_num) {
    bool ok = co_await send_request(cli, i);
    if (ok) {
      end_conn_num.fetch_add(1);
      retry_cnt = 0;
      i++;
    } else {
      retry_cnt++;
      if (retry_cnt == max_retry_cnt) {
        end_conn_num.fetch_add(conn_num - i);
      }
      co_await cgo::sleep(cgo::this_coroutine_ctx(), std::chrono::seconds(1));
    }
  }
}

cgo::Coroutine<void> run_server(size_t port, bool& end_svr_flag) {
  auto& ctx = cgo::this_coroutine_ctx();
  auto sock = cgo::Socket::create(ctx, cgo::Socket::Protocol::TCP, cgo::Socket::AddressFamily::IPv4);
  auto guard = cgo::defer([&sock]() { sock.close(); });
  sock.bind("0.0.0.0", port);
  sock.listen(back_log);
  while (!end_svr_flag) {
    auto conn = co_await sock.accept();
    if (!conn) {
      ::printf("server: accept error: '%s'\n", conn.error().err_msg.data());
      continue;
    }
    cgo::spawn(ctx, handle_request(*conn));
  }
}

TEST(socket, simple) {
  std::atomic<size_t> end_conn_num = 0;
  bool end_svr_flag = false;

  cgo::Context svr_ctx, cli_ctx;
  svr_ctx.start(1);
  cli_ctx.start(exec_num);

  cgo::spawn(svr_ctx, run_server(port, end_svr_flag));
  for (int i = 0; i < cli_num; ++i) {
    cgo::spawn(cli_ctx, run_client(i, end_conn_num));
  }
  auto prev_check_tp = std::chrono::steady_clock::now();
  while (end_conn_num < cli_num * conn_num) {
    auto now = std::chrono::steady_clock::now();
    if (now - prev_check_tp > std::chrono::milliseconds(500)) {
      ::printf("progress: %lu/%lu\n", end_conn_num.load(), cli_num * conn_num);
      prev_check_tp = now;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  end_svr_flag = true;
  cli_ctx.stop();
  svr_ctx.stop();
}

TEST(socket, ctx_stop) {
  std::atomic<size_t> end_conn_num = 0;
  bool end_svr_flag = false;

  cgo::Context svr_ctx, cli_ctx;
  svr_ctx.start(1);
  cli_ctx.start(exec_num);

  cgo::spawn(svr_ctx, run_server(port, end_svr_flag));
  for (int i = 0; i < cli_num; ++i) {
    cgo::spawn(cli_ctx, run_client(i, end_conn_num));
  }
  auto prev_check_tp = std::chrono::steady_clock::now();
  while (end_conn_num < cli_num * conn_num / 2) {
    auto now = std::chrono::steady_clock::now();
    if (now - prev_check_tp > std::chrono::milliseconds(500)) {
      ::printf("progress: %lu/%lu\n", end_conn_num.load(), cli_num * conn_num);
      prev_check_tp = now;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  svr_ctx.stop();
  std::this_thread::sleep_for(std::chrono::seconds(2));
  cli_ctx.stop();
}