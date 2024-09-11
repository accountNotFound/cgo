#pragma once

#include <thread>

#include "core/schedule.h"

namespace cgo::_impl::_ctx {

class Context {
 public:
  void start(size_t n_worker);

  void stop();

  void run_worker(size_t index);

 private:
  std::vector<std::thread> _workers;
  bool _finished = false;
};

inline std::unique_ptr<Context> g_context = nullptr;

inline Context& get_context() { return *g_context; }

}  // namespace cgo::_impl::_ctx

namespace cgo {

void start_context(size_t n_worker);

void stop_context();

}  // namespace cgo
