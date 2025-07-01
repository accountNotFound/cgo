#include "core/context.h"

#include <iostream>

#include "core/event.h"
#include "core/timer.h"

namespace cgo::_impl::_ctx {

void Context::start(size_t worker_num) {
  for (size_t i = 0; i < worker_num; i++) {
    this->_workers.emplace_back(&Context::run_worker, this, i);
  }
}

void Context::stop() {
  this->_finished = true;
  for (auto& worker : this->_workers) {
    if (worker.joinable()) {
      worker.join();
    }
  }
}

void Context::run_worker(size_t index) {
  _impl::EventSignal signal;
  auto guard = defer([&signal]() { signal.close(); });

  _impl::_sched::get_dispatcher().regist(index, signal);
  _impl::_time::get_dispatcher().regist(index, signal);

  auto handle_time = std::chrono::steady_clock::now();
  while (!this->_finished) {
    bool sched_flag = false;
    if (auto task = _impl::_sched::get_dispatcher().dispatch(index); task) {
      _impl::_sched::TaskExecutor::execute(task);
      sched_flag = true;
    }

    bool timer_flag = false;
    auto wait_timeout = std::chrono::milliseconds::max();
    if (auto delayed = _impl::_time::get_dispatcher().dispatch(index); delayed) {
      delayed.fn();
      timer_flag = true;
    } else {
      wait_timeout = std::chrono::duration_cast<decltype(wait_timeout)>(delayed.ex - std::chrono::steady_clock::now());
    }

    if (sched_flag || timer_flag) {
      auto now = std::chrono::steady_clock::now();
      if (now - handle_time > std::chrono::milliseconds(1)) {
        _impl::_event::get_dispatcher().handle(index, 128, std::chrono::milliseconds(0));
        handle_time = now;
      }
    } else {
      _impl::_event::get_dispatcher().handle(index, 128, std::min(wait_timeout, std::chrono::milliseconds(50)));
    }
  }
}

}  // namespace cgo::_impl::_ctx

namespace cgo {

void start_context(size_t n_worker) {
  if (_impl::_ctx::g_context) {
    std::cerr << "context is running\n";
    std::terminate();
  }
  _impl::_ctx::g_context = std::make_unique<_impl::_ctx::Context>();
  _impl::_sched::g_dispatcher = std::make_unique<_impl::_sched::TaskDispatcher>(n_worker);
  _impl::_time::g_dispatcher = std::make_unique<_impl::_time::DelayedDispatcher>(n_worker);
  _impl::_event::g_dispatcher = std::make_unique<_impl::_event::EventDispatcher>(n_worker);
  _impl::_ctx::get_context().start(n_worker);
}

void stop_context() {
  _impl::_ctx::get_context().stop();
  _impl::_event::g_dispatcher = nullptr;
  _impl::_time::g_dispatcher = nullptr;
  _impl::_sched::g_dispatcher = nullptr;
  _impl::_ctx::g_context = nullptr;
}

}  // namespace cgo
