#include "core/channel.h"
#include "core/context.h"
#include "core/event.h"
#include "core/timed.h"
#include "mtest.h"

const size_t exec_num = 4;
const size_t cli_num = 1000, conn_num = 100;

const size_t port = 8080;
const size_t udp_port = 8081;  // 新增 UDP 测试端口
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

// ===================== UDP 测试部分 =====================
cgo::Coroutine<void> run_udp_server(size_t port, std::atomic<size_t>& packet_count, bool& end_svr_flag) {
  auto& ctx = cgo::this_coroutine_ctx();
  auto sock = cgo::Socket::create(ctx, cgo::Socket::Protocol::UDP, cgo::Socket::AddressFamily::IPv4);
  auto guard = cgo::defer([&sock]() { sock.close(); });

  // 绑定 UDP 服务器
  get_or_raise(sock.bind("0.0.0.0", port));

  ::printf("UDP server started on port %zu\n", port);

  while (!end_svr_flag) {
    try {
      // 接收数据包
      auto result = co_await sock.recv_from(1024, std::chrono::seconds(1));
      if (!result) {
        continue;  // 超时或其他错误
      }

      auto& [data, source] = *result;
      auto& [client_ip, client_port] = source;

      // 统计接收到的包
      packet_count.fetch_add(1);

      // 构造回显消息
      std::string echo_msg = "ECHO: " + data;

      // 发送回复
      auto send_result = co_await sock.send_to(echo_msg, client_ip, client_port);
      if (!send_result) {
        ::printf("UDP server send error: %s\n", send_result.error().err_msg.c_str());
      }
    } catch (const cgo::Socket::Error& e) {
      ::printf("UDP server error: %s\n", e.err_msg.c_str());
    }
  }
}

cgo::Coroutine<void> run_udp_client(int id, size_t port, int packets_to_send, std::atomic<size_t>& received_count) {
  auto& ctx = cgo::this_coroutine_ctx();
  auto sock = cgo::Socket::create(ctx, cgo::Socket::Protocol::UDP, cgo::Socket::AddressFamily::IPv4);
  auto guard = cgo::defer([&sock]() { sock.close(); });

  // 客户端绑定随机端口
  get_or_raise(sock.bind("0.0.0.0", 0));

  for (int i = 0; i < packets_to_send;) {
    try {
      // 构造消息
      std::string msg = format("Client %d packet %d", id, i);

      // 发送消息
      auto send_result = co_await sock.send_to(msg, "127.0.0.1", port);
      if (!send_result) {
        ::printf("Client %d send error: %s\n", id, send_result.error().err_msg.c_str());
        continue;
      }

      // 等待回复
      auto recv_result = co_await sock.recv_from(1024, std::chrono::seconds(1));
      if (!recv_result) {
        ::printf("Client %d recv timeout\n", id);
        continue;
      }

      auto& [data, source] = *recv_result;
      if (data.find("ECHO:") == 0) {
        received_count.fetch_add(1);
        ++i;
      }
    } catch (const cgo::Socket::Error& e) {
      ::printf("Client %d error: %s\n", id, e.err_msg.c_str());
    }

    // 稍微延迟以避免洪水攻击
    co_await cgo::sleep(ctx, std::chrono::milliseconds(10));
  }
}

// 简单 UDP 测试
TEST(socket, udp_simple) {
  const size_t packets_to_send = 10;
  std::atomic<size_t> server_packet_count = 0;
  std::atomic<size_t> client_received_count = 0;
  bool end_svr_flag = false;

  cgo::Context ctx;
  ctx.start(2);

  // 启动 UDP 服务器
  cgo::spawn(ctx, run_udp_server(udp_port, server_packet_count, end_svr_flag));

  // 启动单个 UDP 客户端
  cgo::spawn(ctx, run_udp_client(0, udp_port, packets_to_send, client_received_count));

  // 等待客户端完成
  while (client_received_count < packets_to_send) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  end_svr_flag = true;
  ctx.stop();

  ASSERT(server_packet_count.load() == packets_to_send, "");
  ASSERT(client_received_count.load() == packets_to_send, "");
  ::printf("UDP simple test: Server received %zu packets, Client received %zu replies\n", server_packet_count.load(),
           client_received_count.load());
}

// 多客户端 UDP 测试
TEST(socket, udp_multiple_clients) {
  const size_t num_clients = cli_num;
  const size_t packets_per_client = conn_num;
  const size_t total_packets = num_clients * packets_per_client;

  std::atomic<size_t> server_packet_count = 0;
  std::atomic<size_t> client_received_count = 0;
  bool end_svr_flag = false;

  cgo::Context svr_ctx, cli_ctx;
  svr_ctx.start(1);
  cli_ctx.start(exec_num);

  // 启动 UDP 服务器
  cgo::spawn(svr_ctx, run_udp_server(udp_port, server_packet_count, end_svr_flag));

  // 启动多个 UDP 客户端
  for (int i = 0; i < num_clients; ++i) {
    cgo::spawn(cli_ctx, run_udp_client(i, udp_port, packets_per_client, client_received_count));
  }

  // 等待所有包完成
  auto start = std::chrono::steady_clock::now();
  while (client_received_count < total_packets) {
    auto now = std::chrono::steady_clock::now();
    if (now - start > std::chrono::seconds(10)) {
      break;  // 超时
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  end_svr_flag = true;
  svr_ctx.stop();
  cli_ctx.stop();

  ::printf("UDP multiple clients test:\n");
  ::printf("  Total packets sent: %zu\n", total_packets);
  ::printf("  Server received: %zu\n", server_packet_count.load());
  ::printf("  Clients received replies: %zu\n", client_received_count.load());

  // 由于 UDP 不可靠，可能丢失少量包，但大部分应该成功
  ASSERT(server_packet_count.load() >= total_packets * 0.9, "");
  ASSERT(client_received_count.load() >= total_packets * 0.9, "");
}