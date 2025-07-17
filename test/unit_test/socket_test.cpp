#include <cmath>
#include <iostream>

#include "core/channel.h"
#include "core/context.h"
#include "core/event.h"
#include "core/timed.h"
#include "mtest.h"

struct Config {
 public:
  bool is_v6 = false;
  std::string svr_ip = "127.0.0.1";
  uint16_t svr_port = 8080;
  size_t sock_timeout_ms = 2000;

  size_t cli_num = 800;
  size_t cli_interval_ms = 0;
  size_t req_pkg_nbytes = 1024;
  size_t res_pgk_nbytes = 1024;
  size_t svr_listen_size = 65535;

  size_t cli_ctx_threads = 1;
  size_t svr_ctx_threads = 1;

  size_t test_duration_sec = 10;

  std::string str() const {
    std::ostringstream oss;
    oss << "address family: " << (is_v6 ? "Ipv6" : "Ipv4") << "\n";
    oss << "socket timeout ms: " << sock_timeout_ms << "\n";
    oss << "server thread: " << svr_ctx_threads << "\n";
    oss << "client thread: " << cli_ctx_threads << "\n";
    oss << "client num: " << cli_num << "\n";
    oss << "client interval ms: " << cli_interval_ms << "\n";
    oss << "test duration sec: " << test_duration_sec << "\n";
    return oss.str();
  }
};

struct Metric {
 public:
  class Scalar {
   public:
    Scalar(int init) : _value(init) {}

    Scalar& inc() {
      _value.fetch_add(1);
      return *this;
    }

    size_t get() const { return _value.load(); }

   private:
    std::atomic<size_t> _value = 0;
  };

  class Distribution {
   public:
    struct Stats {
      double min = 0;
      double max = 0;
      double avg = 0;
      double std = 0;
      double p50 = 0;
      double p75 = 0;
      double p90 = 0;
      double p95 = 0;
      double p99 = 0;
    };

    Distribution(double low, double high, int bins) : _low(low), _high(high), _bins(bins), _min(high), _max(low) {}

    Distribution& update(double x) {
      _min = std::min(x, _min);
      _max = std::max(x, _max);
      double step = (_high - _low) / _bins.size();
      int slot = (x - _low) / step;
      _bins[slot].fetch_add(1);
      return *this;
    }

    Stats stat() {
      Stats result;
      result.min = (_min == _high ? _low : _min);
      result.max = (_max == _low ? _low : _max);

      // 计算总样本数
      size_t total = 0;
      for (const auto& b : _bins) {
        total += b.load();
      }
      if (total == 0) return result;  // 无数据时返回默认值

      // 计算均值
      double sum = 0.0;
      double step = (_high - _low) / _bins.size();
      for (size_t i = 0; i < _bins.size(); ++i) {
        double midpoint = _low + (i + 0.5) * step;
        sum += midpoint * _bins[i].load();
      }
      result.avg = sum / total;

      // 计算标准差
      double variance = 0.0;
      for (size_t i = 0; i < _bins.size(); ++i) {
        double midpoint = _low + (i + 0.5) * step;
        double diff = midpoint - result.avg;
        variance += _bins[i].load() * diff * diff;
      }
      result.std = std::sqrt(variance / total);

      // 计算分位数
      auto compute_quantile = [&](double q) -> double {
        size_t target = static_cast<size_t>(q * total);
        size_t cumulative = 0;
        for (size_t i = 0; i < _bins.size(); ++i) {
          cumulative += _bins[i].load();
          if (cumulative >= target) {
            return _low + i * step;
          }
        }
        return _high;  // 兜底
      };

      result.p50 = compute_quantile(0.50);
      result.p75 = compute_quantile(0.75);
      result.p90 = compute_quantile(0.90);
      result.p95 = compute_quantile(0.95);
      result.p99 = compute_quantile(0.99);

      return result;
    }

   private:
    double _low;
    double _high;
    std::vector<std::atomic<size_t>> _bins;

    double _min;
    double _max;
  };

  Scalar conn_total_num = 0;
  Scalar conn_error_num = 0;
  Scalar send_total_num = 0;
  Scalar send_error_num = 0;
  Scalar recv_total_num = 0;
  Scalar recv_error_num = 0;
  Distribution conn_latency_ms = {0, 5000, 1000};
  Distribution send_latency_ms = {0, 5000, 1000};
  Distribution recv_latency_ms = {0, 5000, 1000};

  std::string str() {
    std::ostringstream oss;

    // 打印 Scalar 指标
    oss << "conn_total_num: " << conn_total_num.get() << "\n";
    oss << "conn_error_num: " << conn_error_num.get() << ", (" << 100.0 * conn_error_num.get() / conn_total_num.get()
        << "%)\n";
    oss << "send_total_num: " << send_total_num.get() << "\n";
    oss << "send_error_num: " << send_error_num.get() << ", (" << 100.0 * send_error_num.get() / send_total_num.get()
        << "%)\n";
    oss << "recv_total_num: " << recv_total_num.get() << "\n";
    oss << "recv_error_num: " << recv_error_num.get() << ", (" << 100.0 * recv_error_num.get() / recv_total_num.get()
        << "%)\n";

    // 打印 Distribution 指标
    auto print_dist = [&](Distribution& dist, const std::string& name) {
      auto stats = dist.stat();
      oss << name << ":\n";
      oss << "  min=" << stats.min << ", max=" << stats.max << ", avg=" << stats.avg << ", std=" << stats.std << "\n";
      oss << "  p50=" << stats.p50 << ", p75=" << stats.p75 << ", p90=" << stats.p90 << ", p95=" << stats.p95
          << ", p99=" << stats.p99 << "\n";
    };

    print_dist(conn_latency_ms, "conn_latency_ms");
    print_dist(send_latency_ms, "send_latency_ms");
    print_dist(recv_latency_ms, "recv_latency_ms");

    return oss.str();
  }
};

std::string generate_test_data(size_t size) {
  static const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
  std::string data;
  data.reserve(size);

  for (size_t i = 0; i < size; ++i) {
    data += charset[rand() % (sizeof(charset) - 1)];
  }

  return data;
}

cgo::Coroutine<void> tcp_client(const Config& conf, Metric& metric, std::atomic<size_t>& wg) {
  auto& ctx = cgo::this_coroutine_ctx();
  auto sock = cgo::Socket::create(ctx, cgo::Socket::Protocol::TCP,
                                  conf.is_v6 ? cgo::Socket::AddressFamily::IPv6 : cgo::Socket::AddressFamily::IPv4);
  auto guard = cgo::defer([&sock, &wg]() {
    sock.close();
    wg.fetch_add(1);
  });
  auto end_tp = std::chrono::steady_clock::now() + std::chrono::seconds(conf.test_duration_sec);

  bool disconnect = true;
  while (std::chrono::steady_clock::now() < end_tp) {
    co_await cgo::sleep(ctx, std::chrono::milliseconds(conf.cli_interval_ms));

    if (disconnect) {
      metric.conn_total_num.inc();

      auto begin = std::chrono::steady_clock::now();
      auto res = co_await sock.connect(conf.svr_ip, conf.svr_port, std::chrono::milliseconds(conf.sock_timeout_ms));
      if (res) {
        auto end = std::chrono::steady_clock::now();
        metric.conn_latency_ms.update((end - begin).count() / 1e6);
        disconnect = false;
      } else {
        metric.conn_error_num.inc();
        continue;
      }
    }
    {
      metric.send_total_num.inc();

      auto req = generate_test_data(conf.req_pkg_nbytes);
      auto begin = std::chrono::steady_clock::now();
      auto ok = co_await sock.send(req, std::chrono::milliseconds(conf.sock_timeout_ms));
      if (ok) {
        auto end = std::chrono::steady_clock::now();
        metric.send_latency_ms.update((end - begin).count() / 1e6);
      } else {
        metric.send_error_num.inc();
        // disconnect = true;
        continue;
      }
    }
    {
      metric.recv_total_num.inc();

      auto begin = std::chrono::steady_clock::now();
      auto res = co_await sock.recv(conf.res_pgk_nbytes, std::chrono::milliseconds(conf.sock_timeout_ms));
      if (res) {
        auto end = std::chrono::steady_clock::now();
        metric.recv_latency_ms.update((end - begin).count() / 1e6);
      } else {
        auto end = std::chrono::steady_clock::now();
        metric.recv_error_num.inc();
        // disconnect = true;
        continue;
      }
    }
  }
}

cgo::Coroutine<void> tcp_session(const Config& conf, cgo::Socket sock, Metric& metric, std::atomic<size_t>& wg) {
  auto guard = cgo::defer([&sock, &wg]() {
    sock.close();
    wg.fetch_add(1);
  });
  while (true) {
    {
      metric.recv_total_num.inc();
      auto begin = std::chrono::steady_clock::now();
      auto req = co_await sock.recv(conf.req_pkg_nbytes, std::chrono::milliseconds(conf.sock_timeout_ms));
      if (req) {
        auto end = std::chrono::steady_clock::now();
        metric.recv_latency_ms.update((end - begin).count() / 1e6);
      } else {
        metric.recv_error_num.inc();
        co_return;
      }
    }
    {
      metric.send_total_num.inc();
      auto begin = std::chrono::steady_clock::now();
      auto res = generate_test_data(conf.res_pgk_nbytes);
      auto ok = co_await sock.send(res, std::chrono::milliseconds(conf.sock_timeout_ms));
      if (ok) {
        auto end = std::chrono::steady_clock::now();
        metric.send_latency_ms.update((end - begin).count() / 1e6);
      } else {
        metric.send_error_num.inc();
        co_return;
      }
    }
  }
}

cgo::Coroutine<void> tcp_server(const Config& conf, Metric& metric) {
  auto& ctx = cgo::this_coroutine_ctx();
  auto sock = cgo::Socket::create(ctx, cgo::Socket::Protocol::TCP,
                                  conf.is_v6 ? cgo::Socket::AddressFamily::IPv6 : cgo::Socket::AddressFamily::IPv4);
  auto guard = cgo::defer([&sock]() { sock.close(); });

  sock.bind(conf.is_v6 ? "::" : "0.0.0.0", conf.svr_port);
  sock.listen(conf.svr_listen_size);

  cgo::Context session_ctx;
  size_t session_num = 0;
  std::atomic<size_t> session_wg = 0;
  session_ctx.start(conf.svr_ctx_threads);

  auto session_guard = cgo::defer([&session_ctx, &session_wg, session_num]() {
    while (session_wg.load() < session_num) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    session_ctx.stop();
  });

  while (true) {
    auto conn = co_await sock.accept(session_ctx);
    if (!conn) {
      continue;
    }
    metric.conn_total_num.inc();
    cgo::spawn(session_ctx, tcp_session(conf, *conn, metric, session_wg));
  }
}

void tcp_benchmark_test(Config& conf) {
  Metric svr_metric;
  cgo::Context svr_ctx;
  svr_ctx.start(1);
  cgo::spawn(svr_ctx, tcp_server(conf, svr_metric));

  Metric cli_metric;
  cgo::Context cli_ctx;
  cli_ctx.start(conf.cli_ctx_threads);
  std::atomic<size_t> cli_wg = 0;
  for (int i = 0; i < conf.cli_num; ++i) {
    cgo::spawn(cli_ctx, tcp_client(conf, cli_metric, cli_wg));
  }

  while (cli_wg.load() < conf.cli_num) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  cli_ctx.stop();
  svr_ctx.stop();

  ::printf("TCP benchmark result:\n");
  ::printf("Config:\n");
  std::cout << conf.str();
  // ::printf("Server:\n");
  // std::cout << svr_metric.str();
  ::printf("Client:\n");
  std::cout << cli_metric.str();
}

TEST(socket, tcp_bench) {
  Config conf;
  conf.is_v6 = false;
  conf.svr_ip = conf.is_v6 ? "::" : "0.0.0.0";
  conf.test_duration_sec = 10;
  conf.svr_port = 8080;
  conf.sock_timeout_ms = 2000;
  conf.cli_num = 1000;
  conf.cli_interval_ms = 50;
  conf.cli_ctx_threads = 1;
  conf.svr_ctx_threads = 1;
  conf.req_pkg_nbytes = 1024;
  conf.res_pgk_nbytes = 1024;
  tcp_benchmark_test(conf);
  std::cout << "\n\n";
}

cgo::Coroutine<void> udp_client(const Config& conf, Metric& metric, std::atomic<size_t>& wg) {
  auto& ctx = cgo::this_coroutine_ctx();
  auto sock = cgo::Socket::create(ctx, cgo::Socket::Protocol::UDP,
                                  conf.is_v6 ? cgo::Socket::AddressFamily::IPv6 : cgo::Socket::AddressFamily::IPv4);
  auto guard = cgo::defer([&sock, &wg]() {
    sock.close();
    wg.fetch_add(1);
  });
  sock.bind(conf.is_v6 ? "::" : "0.0.0.0", 0);
  auto end_tp = std::chrono::steady_clock::now() + std::chrono::seconds(conf.test_duration_sec);

  bool disconnect = true;
  std::string svr_ip;
  uint16_t svr_port;
  while (std::chrono::steady_clock::now() < end_tp) {
    co_await cgo::sleep(ctx, std::chrono::milliseconds(conf.cli_interval_ms));
    if (disconnect) {
      metric.conn_total_num.inc();
      auto begin = std::chrono::steady_clock::now();
      if (auto ok = co_await sock.sendto("1", conf.svr_ip, conf.svr_port); !ok) {
        metric.conn_error_num.inc();
        continue;
      }
      if (auto res = co_await sock.recvfrom(1, std::chrono::milliseconds(conf.sock_timeout_ms)); !res) {
        metric.conn_error_num.inc();
        continue;
      } else {
        auto end = std::chrono::steady_clock::now();
        metric.conn_latency_ms.update((end - begin).count() / 1e6);
        auto& [data, source] = *res;
        svr_ip = source.first;
        svr_port = source.second;
        disconnect = false;
      }
    }
    {
      metric.send_total_num.inc();
      auto req = generate_test_data(conf.req_pkg_nbytes);
      auto begin = std::chrono::steady_clock::now();
      auto ok = co_await sock.sendto(req, svr_ip, svr_port);
      if (ok) {
        auto end = std::chrono::steady_clock::now();
        metric.send_latency_ms.update((end - begin).count() / 1e6);
      } else {
        metric.send_error_num.inc();
        disconnect = true;
        continue;
      }
    }
    {
      metric.recv_total_num.inc();
      auto begin = std::chrono::steady_clock::now();
      auto res = co_await sock.recvfrom(conf.res_pgk_nbytes, std::chrono::milliseconds(conf.sock_timeout_ms));
      if (res) {
        auto end = std::chrono::steady_clock::now();
        metric.recv_latency_ms.update((end - begin).count() / 1e6);
      } else {
        metric.recv_error_num.inc();
        disconnect = true;
        continue;
      }
    }
  }
}

cgo::Coroutine<void> udp_session(const Config& conf, std::string cli_ip, uint16_t cli_port, Metric& metric,
                                 std::atomic<size_t>& wg) {
  auto& ctx = cgo::this_coroutine_ctx();
  auto sock = cgo::Socket::create(ctx, cgo::Socket::Protocol::UDP,
                                  conf.is_v6 ? cgo::Socket::AddressFamily::IPv6 : cgo::Socket::AddressFamily::IPv4);
  auto guard = cgo::defer([&sock, &wg]() {
    sock.close();
    wg.fetch_add(1);
  });
  sock.bind(conf.is_v6 ? "::" : "0.0.0.0", 0);

  auto ok = co_await sock.sendto("1", cli_ip, cli_port, std::chrono::milliseconds(conf.sock_timeout_ms));
  if (!ok) {
    co_return;
  }
  metric.conn_total_num.inc();

  while (true) {
    {
      metric.recv_total_num.inc();
      auto begin = std::chrono::steady_clock::now();
      auto req = co_await sock.recvfrom(conf.req_pkg_nbytes, std::chrono::milliseconds(conf.sock_timeout_ms));
      if (req) {
        auto end = std::chrono::steady_clock::now();
        metric.recv_latency_ms.update((end - begin).count() / 1e6);
      } else {
        metric.recv_error_num.inc();
        co_return;
      }
    }
    {
      metric.send_total_num.inc();
      auto res = generate_test_data(conf.res_pgk_nbytes);
      auto begin = std::chrono::steady_clock::now();
      auto ok = co_await sock.sendto(res, cli_ip, cli_port, std::chrono::milliseconds(conf.sock_timeout_ms));
      if (ok) {
        auto end = std::chrono::steady_clock::now();
        metric.send_latency_ms.update((end - begin).count() / 1e6);
      } else {
        metric.send_error_num.inc();
        co_return;
      }
    }
  }
}

cgo::Coroutine<void> udp_server(const Config& conf, Metric& metric) {
  auto& ctx = cgo::this_coroutine_ctx();
  auto sock = cgo::Socket::create(ctx, cgo::Socket::Protocol::UDP,
                                  conf.is_v6 ? cgo::Socket::AddressFamily::IPv6 : cgo::Socket::AddressFamily::IPv4);
  auto guard = cgo::defer([&sock]() { sock.close(); });

  sock.bind(conf.is_v6 ? "::" : "0.0.0.0", conf.svr_port);

  cgo::Context session_ctx;
  size_t session_num = 0;
  std::atomic<size_t> session_wg = 0;

  session_ctx.start(conf.svr_ctx_threads);
  auto session_guard = cgo::defer([&session_ctx, &session_wg, session_num]() {
    while (session_wg.load() < session_num) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    session_ctx.stop();
  });

  while (true) {
    {
      auto req = co_await sock.recvfrom(1);
      if (!req) {
        continue;
      }
      auto& [data, source] = *req;
      auto& [cli_ip, cli_port] = source;

      session_num++;
      cgo::spawn(session_ctx, udp_session(conf, cli_ip, cli_port, metric, session_wg));
    }
  }
}

void udp_benchmark_test(Config& conf) {
  Metric svr_metric;
  cgo::Context svr_ctx;
  svr_ctx.start(1);
  cgo::spawn(svr_ctx, udp_server(conf, svr_metric));

  Metric cli_metric;
  cgo::Context cli_ctx;
  cli_ctx.start(conf.cli_ctx_threads);
  std::atomic<size_t> cli_wg = 0;
  for (int i = 0; i < conf.cli_num; ++i) {
    cgo::spawn(cli_ctx, udp_client(conf, cli_metric, cli_wg));
  }

  while (cli_wg.load() < conf.cli_num) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  cli_ctx.stop();
  svr_ctx.stop();

  ::printf("UDP benchmark result:\n");
  ::printf("Config:\n");
  std::cout << conf.str();
  // ::printf("Server:\n");
  // std::cout << svr_metric.str();
  ::printf("Client:\n");
  std::cout << cli_metric.str();
}

TEST(socket, udp_bench) {
  Config conf;
  conf.is_v6 = true;
  conf.svr_ip = conf.is_v6 ? "::" : "0.0.0.0";
  conf.test_duration_sec = 10;
  conf.svr_port = 8081;
  conf.sock_timeout_ms = 2000;
  conf.cli_num = 1000;
  conf.cli_interval_ms = 50;
  conf.cli_ctx_threads = 1;
  conf.svr_ctx_threads = 1;
  conf.req_pkg_nbytes = 1024;
  conf.res_pgk_nbytes = 1024;
  udp_benchmark_test(conf);
}