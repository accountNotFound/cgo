#include "core/context.h"

#include <iostream>

namespace cgo::_impl::_ctx {

void Context::start(size_t worker_num) {
  _impl::_sched::g_dispatcher = std::make_unique<_impl::_sched::TaskDispatcher>(worker_num);
  for (size_t i = 0; i < worker_num; i++) {
    this->_workers.emplace_back([this, i]() { this->run_worker(i); });
  }
}

void Context::stop() {
  this->_finished = true;
  for (auto& worker : this->_workers) {
    if (worker.joinable()) {
      worker.join();
    }
  }
  _impl::_sched::g_dispatcher = nullptr;
}

void Context::run_worker(size_t index) {
  while (!this->_finished) {
    if (auto task = _impl::_sched::get_dispatcher().dispatch(index); task) {
      _impl::_sched::TaskExecutor::execute(task);
      continue;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
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
  _impl::_ctx::get_context().start(n_worker);
}

void stop_context() {
  _impl::_ctx::get_context().stop();
  _impl::_ctx::g_context = nullptr;
}

}  // namespace cgo
