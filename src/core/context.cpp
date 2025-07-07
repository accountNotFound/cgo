#include "core/context.h"

#include <iostream>

#include "core/schedule.h"

namespace cgo::_impl {

auto SchedContext::at(Context& ctx) -> SchedContext& { return *ctx._sched_ctx; }

auto TimedContext::at(Context& ctx) -> TimedContext& { return *ctx._timed_ctx; }

}  // namespace cgo::_impl

namespace cgo {

void Context::start(size_t n_worker) {
  if (_finished) {
    std::cerr << "try to start but context is stopped\n";
    std::exit(EXIT_FAILURE);
  }
  _sched_ctx = std::make_unique<_impl::SchedContext>(*this, n_worker);
  _timed_ctx = std::make_unique<_impl::TimedContext>(n_worker);
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

  _timed_ctx = nullptr;
  _sched_ctx = nullptr;
}

void Context::_run(size_t pindex) {
  _impl::BaseLazySignal signal;
  _sched_ctx->on_scheduled(pindex, signal);
  _timed_ctx->on_timeout(pindex, signal);
  while (!_finished) {
    bool sched_flag = false;
    if (_sched_ctx->run_scheduled(pindex, 128) > 0) {
      sched_flag = true;
    }

    bool timed_flag = false;
    if (_timed_ctx->run_timeout(pindex, 128) > 0) {
      timed_flag = true;
    }

    if (sched_flag || timed_flag) {
      continue;
    }
    signal.wait(std::chrono::milliseconds(50));
  }
}

}  // namespace cgo
