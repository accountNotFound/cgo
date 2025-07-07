#include "core/context.h"

#include <iostream>

#include "core/schedule.h"

namespace cgo::_impl {

auto SchedContext::at(Context& ctx) -> SchedContext& { return *ctx._sched_ctx; }

}  // namespace cgo::_impl

namespace cgo {

void Context::start(size_t n_worker) {
  if (_finished) {
    std::cerr << "try to start but context is stopped\n";
    std::exit(EXIT_FAILURE);
  }
  _sched_ctx = std::make_unique<_impl::SchedContext>(*this, n_worker);
  for (int i = 0; i < n_worker; ++i) {
    _workers.emplace_back(&Context::_run, this, i);
  }
}

void Context::stop() {
  if (_finished) {
    return;
  }
  _finished = true;
  for (auto& worker : _workers) {
    if (worker.joinable()) {
      worker.join();
    }
  }
  _sched_ctx->clear();
  _sched_ctx = nullptr;
}

void Context::_run(size_t pindex) {
  _impl::BaseLazySignal signal;
  _sched_ctx->on_scheduled(pindex, signal);
  while (!_finished) {
    if (_sched_ctx->run_scheduled(pindex, 128) > 0) {
      continue;
    }
    signal.wait(std::chrono::milliseconds(50));
  }
}

}  // namespace cgo
