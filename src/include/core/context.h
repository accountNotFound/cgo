#pragma once

#include <thread>

#include "core/schedule.h"

namespace cgo::_impl::_ctx {

class Context {
 public:
  void start(size_t worker_num);

  void stop();

  void run_worker(size_t index);

 private:
  std::vector<std::thread> _workers;
  bool _finished = false;
};

extern std::unique_ptr<Context> g_context;

}  // namespace cgo::_impl::_ctx

namespace cgo {

void start_context(size_t worker_num);

void stop_context();

}  // namespace cgo
