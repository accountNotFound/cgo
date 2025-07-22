#include "core/channel.h"

#include "core/context.h"
#include "mtest.h"

const size_t exec_num = 4;
const size_t msg_num = 1e5;

int mod(int a, int b) { return a % b; }

void channel_test(int n_writer, int n_reader, int buffer_size) {
  ASSERT(mod(msg_num, n_reader) == 0 && mod(msg_num, n_writer) == 0, "");

  std::atomic<int> w_res = 0, r_res = 0;
  cgo::Channel<int> chan(buffer_size);

  cgo::_impl::Spinlock mtx;
  std::unordered_map<int, int> cnts;

  auto add = [&mtx, &cnts](int v) {
    std::unique_lock guard(mtx);
    cnts[v]++;
  };

  cgo::Context ctx;
  ctx.startup(exec_num);

  for (int i = 0; i < n_reader; i++) {
    cgo::spawn(ctx,
               [](decltype(r_res)& r_res, decltype(add)& add, decltype(chan) chan,
                  decltype(n_reader) n_reader) -> cgo::Coroutine<void> {
                 for (int i = 0; i < msg_num / n_reader; i++) {
                   int v;
                   co_await (chan >> v);
                   // printf("{%d} recv %d\n", cgo::this_coroutine_id(), v);
                   add(v);
                 }
                 r_res.fetch_add(1);
               }(r_res, add, chan, n_reader));
  }

  for (int i = 0; i < n_writer; i++) {
    cgo::spawn(ctx,
               [](decltype(r_res)& w_res, decltype(chan) chan, decltype(n_writer) n_writer) -> cgo::Coroutine<void> {
                 for (int i = 0; i < msg_num / n_writer; i++) {
                   int v = i;
                   co_await (chan << std::move(v));
                   // printf("{%d} send %d\n", cgo::this_coroutine_id(), v);
                 }
                 w_res.fetch_add(1);
               }(w_res, chan, n_writer));
  }

  while (r_res < n_reader || w_res < n_writer) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  ctx.shutdown();
  ASSERT(r_res == n_reader && w_res == n_writer, "");
  int total_cnt = 0;
  for (int i = 0; i < msg_num / n_writer; i++) {
    ASSERT(cnts.contains(i) && cnts[i] == n_writer, "");
  }
}

// TEST(channel, w1r1b0) { channel_test(1, 1, 0); }

// TEST(channel, w4r4b0) { channel_test(4, 4, 0); }

// TEST(channel, w1r1b1) { channel_test(1, 1, 10); }

// TEST(channel, w4r4b1) { channel_test(4, 4, 10); }

TEST(channel, w2r5b0) { channel_test(2, 5, 0); }

// TEST(channel, w5r2b0) { channel_test(5, 2, 0); }

// TEST(channel, w2r5b1) { channel_test(2, 5, 5); }

TEST(channel, w5r2b1) { channel_test(5, 2, 5); }

void select_test(int n_writer, int n_reader, int buffer_size) {
  ASSERT(mod(msg_num, n_reader) == 0 && mod(msg_num, n_writer) == 0, "");

  std::atomic<int> w_res = 0, r_res = 0;
  std::array<cgo::Channel<int>, 3> chans;
  for (int i = 0; i < 3; i++) {
    chans[i] = cgo::Channel<int>(buffer_size);
  }

  cgo::_impl::Spinlock mtx;
  std::unordered_map<int, int> cnts;

  auto add = [&mtx, &cnts](int v) {
    std::unique_lock guard(mtx);
    cnts[v]++;
  };

  cgo::Context ctx;
  ctx.startup(exec_num);

  for (int i = 0; i < n_reader; i++) {
    cgo::spawn(ctx,
               [](decltype(r_res)& r_res, decltype(chans)& chans, decltype(add)& add,
                  decltype(n_reader) n_reader) -> cgo::Coroutine<void> {
                 for (int i = 0; i < msg_num / n_reader; i++) {
                   std::array<int, 3> vals = {-1, -1, -1};
                   cgo::Select select;
                   select.on(0, chans[0]) >> vals[0];
                   select.on(1, chans[1]) >> vals[1];
                   select.on(2, chans[2]) >> vals[2];
                   int key = co_await select();
                   // printf("[%d] recv{%d} from key=%d, val=%d\n", cgo::this_coroutine_id(), i, key, vals[key]);
                   ASSERT(vals[key] == key, "key=%d, val=%d\n", key, vals[key]);
                   add(key);
                 }
                 r_res.fetch_add(1);
               }(r_res, chans, add, n_reader));
  }

  for (int i = 0; i < n_writer; i++) {
    cgo::spawn(ctx,
               [](decltype(w_res)& w_res, decltype(chans)& chans, decltype(n_writer) n_writer) -> cgo::Coroutine<void> {
                 for (int i = 0; i < msg_num / n_writer; i++) {
                   cgo::Select select;
                   select.on(0, chans[0]) << 0;
                   select.on(1, chans[1]) << 1;
                   select.on(2, chans[2]) << 2;
                   int key = co_await select();
                   // printf("[%d] send{%d} to key=%d, val=%d\n", cgo::this_coroutine_id(), i, key, key);
                 }
                 w_res.fetch_add(1);
               }(w_res, chans, n_writer));
  }

  while (r_res < n_reader || w_res < n_writer) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  ctx.shutdown();
  ASSERT(r_res == n_reader && w_res == n_writer, "");

  int recv_cnt = 0;
  for (auto& [_, cnt] : cnts) {
    recv_cnt += cnt;
  }
  ASSERT(recv_cnt == msg_num, "")
}

// TEST(channel, select_w1r1b0) { select_test(1, 1, 0); }

// TEST(channel, select_w4r4b0) { select_test(4, 4, 0); }

// TEST(channel, select_w1r1b1) { select_test(1, 1, 10); }

// TEST(channel, select_w4r4b1) { select_test(4, 4, 10); }

TEST(channel, select_w2r5b0) { select_test(2, 5, 0); }

// TEST(channel, select_w5r2b0) { select_test(5, 2, 0); }

// TEST(channel, select_w2r5b1) { select_test(2, 5, 5); }

TEST(channel, select_w5r2b1) { select_test(5, 2, 5); }

void multi_ctx_nowait_test(int buffer_size) {
  const size_t n_reader = 4;

  std::array<cgo::Channel<int>, n_reader> chans;
  for (int i = 0; i < n_reader; i++) {
    chans[i] = cgo::Channel<int>(buffer_size);
  }

  std::atomic<int> w_res = 0;

  std::vector<int> values(msg_num, -1);

  cgo::Context ctx1, ctx2;
  ctx1.startup(n_reader);
  ctx2.startup(1);

  for (int i = 0; i < n_reader; ++i) {
    cgo::spawn(ctx1, [](decltype(chans[0])& chan, decltype(values)& values) -> cgo::Coroutine<void> {
      while (true) {
        int v;
        co_await (chan >> v);
        ASSERT(values[v] == -1, "v=%d, values[v]=%d\n", v, values[v]);
        values[v] = v;
      }
    }(chans[i], values));
  }

  cgo::spawn(ctx2, [](decltype(chans)& chans, decltype(w_res)& w_res) -> cgo::Coroutine<void> {
    for (int i = 0; i < msg_num;) {
      bool use_nowait = std::rand() % 2;
      int r = std::rand() % n_reader;
      if (use_nowait) {
        if (chans[r].nowait() << i) {
          ++i;
        } else {
          co_await cgo::yield();
        }
      } else {
        cgo::Select select;
        select.on(1, chans[r]) << i;
        select.on(-1, cgo::Select::Default{});
        switch (co_await select()) {
          case 1: {
            ++i;
            break;
          }
          default: {
            co_await cgo::yield();
          }
        }
      }
    }

    // make sure reader pop value out from channel
    cgo::sleep(cgo::this_coroutine_ctx(), std::chrono::milliseconds(10));
    w_res.fetch_add(1);
  }(chans, w_res));

  while (w_res < 1) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  ctx1.shutdown();
  ctx2.shutdown();

  for (int i = 0; i < msg_num; ++i) {
    ASSERT(values[i] == i, "values[%d]==%d\n", i, values[i]);
  }
}

TEST(channel, multi_ctx_nowait_b0) { multi_ctx_nowait_test(0); }

// TEST(channel, multi_ctx_nowait_b2) { multi_ctx_test(2); }

TEST(channel, multi_ctx_nowait_b8) { multi_ctx_nowait_test(8); }

void multi_ctx_stop_test(size_t buffer_size) {
  cgo::Channel<int> chans[3];
  for (int i = 0; i < 3; ++i) {
    chans[i] = cgo::Channel<int>(buffer_size);
  }

  cgo::Context ctx1;
  ctx1.startup(exec_num);
  for (int i = 0; i < msg_num; ++i) {
    cgo::spawn(ctx1, [](decltype(chans)& chans, int i) -> cgo::Coroutine<void> {
      cgo::Select select;
      select.on(0, chans[0]) << i;
      select.on(1, chans[1]) << i;
      select.on(2, chans[2]) << i;
      co_await select();
    }(chans, i));
  }

  std::thread th([&ctx1]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ctx1.shutdown();
  });

  std::atomic<int> res = 0;

  cgo::Context ctx2;
  ctx2.startup(exec_num);
  for (int i = 0; i < msg_num; ++i) {
    cgo::spawn(ctx2, [](decltype(chans)& chans, decltype(res)& res) -> cgo::Coroutine<void> {
      cgo::Select select;
      int v;
      select.on(0, chans[0]) >> v;
      select.on(0, chans[0]) >> v;
      select.on(0, chans[0]) >> v;
      co_await select();
      res.fetch_add(1);
    }(chans, res));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  ctx2.shutdown();
  th.join();
}

TEST(channel, multi_ctx_stop_b0) { multi_ctx_stop_test(0); }

// TEST(channel, multi_ctx_stop_b10) { multi_ctx_stop_test(10); }

TEST(channel, multi_ctx_stop_b100) { multi_ctx_stop_test(100); }
