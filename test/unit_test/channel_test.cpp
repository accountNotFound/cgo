#include "core/channel.h"

#include "core/context.h"
#include "mtest.h"

const size_t exec_num = 4;
const size_t foo_loop = 1000;

void test(int n_reader, int n_writer, int buffer_size) {
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
        int v = co_await chan.recv();
        add(v);
      }
      r_res.fetch_add(1);
    }(r_res, add, chan));
  }

  for (int i = 0; i < n_writer; i++) {
    cgo::spawn([](decltype(r_res)& w_res, decltype(chan) chan) -> cgo::Coroutine<void> {
      for (int i = 0; i < foo_loop; i++) {
        co_await chan.send(i);
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

TEST(channel, w1r1b0) { test(1, 1, 0); }

TEST(channel, w1r1b1) { test(1, 1, 10); }

TEST(channel, w4r4b0) { test(4, 4, 0); }

TEST(channel, w4r4b1) { test(4, 4, 10); }