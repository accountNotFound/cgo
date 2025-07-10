#include "core/context.h"

#include <iostream>

#include "core/schedule.h"

namespace cgo::_impl {

auto SchedContext::at(Context& ctx) -> SchedContext& { return *ctx._sched_ctx; }

auto TimedContext::at(Context& ctx) -> TimedContext& { return *ctx._timed_ctx; }

auto EventContext::at(Context& ctx) -> EventContext& { return *ctx._event_ctx; }

}  // namespace cgo::_impl

namespace cgo {

void Context::start(size_t n_worker) {
  if (_finished) {
    std::cerr << "try to start but context is stopped\n";
    std::exit(EXIT_FAILURE);
  }
  _sched_ctx = std::make_unique<_impl::SchedContext>(*this, n_worker);
  _timed_ctx = std::make_unique<_impl::TimedContext>(n_worker);
  _event_ctx = std::make_unique<_impl::EventContext>(n_worker);
  for (int i = 0; i < n_worker; ++i) {
    _signals.emplace_back(std::make_unique<_impl::EventLazySignal>(*this, i, /*nowait=*/true));
  }
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
}

void Context::_run(size_t pindex) {
  _sched_ctx->on_scheduled(pindex, *_signals[pindex]);
  _timed_ctx->on_timeout(pindex, *_signals[pindex]);

  auto last_handle_time = std::chrono::steady_clock::now();
  while (!_finished) {
    bool sched_flag = false;
    if (_sched_ctx->run_scheduled(pindex, 128) > 0) {
      sched_flag = true;
    }

    bool timed_flag = false;
    if (_timed_ctx->run_timeout(pindex, 128) > 0) {
      timed_flag = true;
    }

    auto now = std::chrono::steady_clock::now();
    auto next_sched_time = _timed_ctx->next_schedule_time(pindex);
    if (sched_flag || timed_flag || now >= next_sched_time) {
      if (now - last_handle_time > std::chrono::milliseconds(1)) {
        _event_ctx->run_handler(pindex, 128, std::chrono::milliseconds(0));
      }
    } else {
      auto timeout = std::chrono::duration_cast<std::chrono::milliseconds>(next_sched_time - now);
      _event_ctx->run_handler(pindex, 128, std::min(timeout, std::chrono::milliseconds(50)));
    }
  }

  _signals[pindex]->close();
  _sched_ctx->final_schedule(pindex);
}

}  // namespace cgo
