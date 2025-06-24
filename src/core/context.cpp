#include "core/context.h"

#include <iostream>

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
  Signal signal;
  _impl::_sched::get_dispatcher().regist(index, signal);
  while (!this->_finished) {
    if (auto task = _impl::_sched::get_dispatcher().dispatch(index); task) {
      _impl::_sched::TaskExecutor::execute(task);
      continue;
    }
    signal.wait(std::chrono::milliseconds(50));
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
  _impl::_ctx::get_context().start(n_worker);
}

void stop_context() {
  _impl::_ctx::get_context().stop();
  _impl::_sched::g_dispatcher = nullptr;
  _impl::_ctx::g_context = nullptr;
}

}  // namespace cgo
