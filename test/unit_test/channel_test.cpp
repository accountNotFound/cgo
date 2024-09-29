#include "core/channel.h"

#include "core/context.h"
#include "mtest.h"

const size_t exec_num = 4;
const size_t foo_loop = 1000;

void channel_test(int n_reader, int n_writer, int buffer_size) {
  std::atomic<int> w_res = 0, r_res = 0;
  cgo::Channel<int> chan(buffer_size);

  cgo::Spinlock mtx;
  std::unordered_map<int, int> cnts;

  auto add = [&mtx, &cnts](int v) {
    std::unique_lock guard(mtx);
    cnts[v]++;
  };

  cgo::start_context(exec_num);

  for (int i = 0; i < n_reader; i++) {
    cgo::spawn([](decltype(r_res)& r_res, decltype(add)& add, decltype(chan) chan) -> cgo::Coroutine<void> {
      for (int i = 0; i < foo_loop; i++) {
        int v;
        co_await (chan >> v);
        add(v);
      }
      r_res.fetch_add(1);
    }(r_res, add, chan));
  }

  for (int i = 0; i < n_writer; i++) {
    cgo::spawn([](decltype(r_res)& w_res, decltype(chan) chan) -> cgo::Coroutine<void> {
      for (int i = 0; i < foo_loop; i++) {
        int v = i;
        co_await (chan << v);
      }
      w_res.fetch_add(1);
    }(w_res, chan));
  }

  while (r_res < n_reader || w_res < n_writer) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  cgo::stop_context();
  ASSERT(r_res == n_reader && w_res == n_writer, "");
  ASSERT(cnts.size() == foo_loop, "");
  for (int i = 0; i < foo_loop; i++) {
    ASSERT(cnts.contains(i) && cnts[i] == n_writer, "");
  }
}

TEST(channel, w1r1b0) { channel_test(1, 1, 0); }

TEST(channel, w1r1b1) { channel_test(1, 1, 10); }

TEST(channel, w4r4b0) { channel_test(4, 4, 0); }

TEST(channel, w4r4b1) { channel_test(4, 4, 10); }

void select_test(int n_reader, int n_writer, int buffer_size) {
  std::atomic<int> w_res = 0, r_res = 0;
  std::array<cgo::Channel<int>, 3> chans;
  for (int i = 0; i < 3; i++) {
    chans[i] = cgo::Channel<int>(buffer_size);
  }

  cgo::Spinlock mtx;
  std::unordered_map<int, int> cnts;

  auto add = [&mtx, &cnts](int v) {
    std::unique_lock guard(mtx);
    cnts[v]++;
  };

  cgo::start_context(exec_num);

  for (int i = 0; i < n_reader; i++) {
    cgo::spawn([](decltype(chans)& chans, decltype(add)& add, decltype(r_res)& r_res) -> cgo::Coroutine<void> {
      for (int i = 0; i < foo_loop; i++) {
        std::array<int, 3> vals = {-1, -1, -1};
        cgo::Select select;
        select.on(0, chans[0]) >> vals[0];
        select.on(1, chans[1]) >> vals[1];
        select.on(2, chans[2]) >> vals[2];
        int key = co_await select(/*with_default=*/false);
        // printf("recv at key=%d, val=%d\n", key, vals[key]);
        ASSERT(vals[key] == key, "key=%d, val=%d\n", key, vals[key]);
        add(key);
      }
      r_res.fetch_add(1);
    }(chans, add, r_res));

    cgo::spawn([](decltype(chans)& chans, decltype(w_res)& w_res) -> cgo::Coroutine<void> {
      for (int i = 0; i < foo_loop; i++) {
        cgo::Select select;
        select.on(0, chans[0]) << 0;
        select.on(1, chans[1]) << 1;
        select.on(2, chans[2]) << 2;
        int key = co_await select(/*with_default=*/false);
      }
      w_res.fetch_add(1);
    }(chans, w_res));
  }

  while (r_res < n_reader || w_res < n_writer) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  cgo::stop_context();
  ASSERT(r_res == n_reader && w_res == n_writer, "");

  int recv_cnt = 0;
  for (auto& [_, cnt] : cnts) {
    recv_cnt += cnt;
  }
  ASSERT(recv_cnt == n_writer * foo_loop, "")
}

TEST(channel, select_r1w1b0) { select_test(1, 1, 0); }

TEST(channel, select_r4w4b0) { select_test(4, 4, 0); }

TEST(channel, select_r1w1b1) { select_test(1, 1, 10); }

TEST(channel, select_r4w4b1) { select_test(4, 4, 10); }