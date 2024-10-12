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

  cgo::Spinlock mtx;
  std::unordered_map<int, int> cnts;

  auto add = [&mtx, &cnts](int v) {
    std::unique_lock guard(mtx);
    cnts[v]++;
  };

  cgo::start_context(exec_num);

  for (int i = 0; i < n_reader; i++) {
    cgo::spawn([](decltype(r_res)& r_res, decltype(add)& add, decltype(chan) chan,
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
    cgo::spawn([](decltype(r_res)& w_res, decltype(chan) chan, decltype(n_writer) n_writer) -> cgo::Coroutine<void> {
      for (int i = 0; i < msg_num / n_writer; i++) {
        int v = i;
        co_await (chan << v);
        // printf("{%d} send %d\n", cgo::this_coroutine_id(), v);
      }
      w_res.fetch_add(1);
    }(w_res, chan, n_writer));
  }

  while (r_res < n_reader || w_res < n_writer) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  cgo::stop_context();
  ASSERT(r_res == n_reader && w_res == n_writer, "");
  int total_cnt = 0;
  for (int i = 0; i < msg_num / n_writer; i++) {
    ASSERT(cnts.contains(i) && cnts[i] == n_writer, "");
  }
}

// TEST(channel, w1r1b0) { channel_test(1, 1, 0); }

// TEST(channel, w1r1b1) { channel_test(1, 1, 10); }

// TEST(channel, w4r4b0) { channel_test(4, 4, 0); }

// TEST(channel, w4r4b1) { channel_test(4, 4, 10); }

TEST(channel, w2r5b0) { channel_test(2, 5, 0); }

TEST(channel, w5r2b0) { channel_test(5, 2, 0); }

TEST(channel, w2r5b1) { channel_test(2, 5, 5); }

TEST(channel, w5r2b1) { channel_test(5, 2, 5); }

void select_test(int n_writer, int n_reader, int buffer_size) {
  ASSERT(mod(msg_num, n_reader) == 0 && mod(msg_num, n_writer) == 0, "");

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
    cgo::spawn([](decltype(r_res)& r_res, decltype(chans)& chans, decltype(add)& add,
                  decltype(n_reader) n_reader) -> cgo::Coroutine<void> {
      for (int i = 0; i < msg_num / n_reader; i++) {
        std::array<int, 3> vals = {-1, -1, -1};
        cgo::Select select;
        select.on(0, chans[0]) >> vals[0];
        select.on(1, chans[1]) >> vals[1];
        select.on(2, chans[2]) >> vals[2];
        int key = co_await select(/*with_default=*/false);
        // printf("[%d] recv{%d} from key=%d, val=%d\n", cgo::this_coroutine_id(), i, key, vals[key]);
        ASSERT(vals[key] == key, "key=%d, val=%d\n", key, vals[key]);
        add(key);
      }
      r_res.fetch_add(1);
    }(r_res, chans, add, n_reader));
  }

  for (int i = 0; i < n_writer; i++) {
    cgo::spawn([](decltype(w_res)& w_res, decltype(chans)& chans, decltype(n_writer) n_writer) -> cgo::Coroutine<void> {
      for (int i = 0; i < msg_num / n_writer; i++) {
        cgo::Select select;
        select.on(0, chans[0]) << 0;
        select.on(1, chans[1]) << 1;
        select.on(2, chans[2]) << 2;
        int key = co_await select(/*with_default=*/false);
        // printf("[%d] send{%d} to key=%d, val=%d\n", cgo::this_coroutine_id(), i, key, key);
      }
      w_res.fetch_add(1);
    }(w_res, chans, n_writer));
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
  ASSERT(recv_cnt == msg_num, "")
}

// TEST(channel, select_w1r1b0) { select_test(1, 1, 0); }

// TEST(channel, select_w4r4b0) { select_test(4, 4, 0); }

// TEST(channel, select_w1r1b1) { select_test(1, 1, 10); }

// TEST(channel, select_w4r4b1) { select_test(4, 4, 10); }

TEST(channel, select_w2r5b0) { select_test(2, 5, 0); }

TEST(channel, select_w5r2b0) { select_test(5, 2, 0); }

TEST(channel, select_w2r5b1) { select_test(2, 5, 5); }

TEST(channel, select_w5r2b1) { select_test(5, 2, 5); }