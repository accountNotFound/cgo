#include "core/context.h"

#include "core/schedule.h"

namespace cgo {

namespace _impl {

namespace _ctx {

struct ContextBase::Impl {
 public:
  bool finished = false;
  _impl::TaskProcessor task_processor;

  Impl(size_t n_partition) : task_processor(n_partition) {}

  void run(Context* ctx, size_t pindex) {
    _impl::LazySignalBase signal;
    this->task_processor.on_scheduled(pindex, signal);
    while (!this->finished) {
      if (this->task_processor.run_scheduled(pindex, 128) > 0) {
        continue;
      }
      signal.wait(std::chrono::milliseconds(50));
    }
  }
};

ContextBase::ContextBase() = default;

ContextBase::~ContextBase() = default;

}  // namespace _ctx

auto TaskProcessor::get(Context* ctx) -> TaskProcessor& {
  return static_cast<_ctx::ContextBase&>(*ctx).impl->task_processor;
}

}  // namespace _impl

void Context::start(size_t n_workers) {
  this->impl = std::make_unique<ContextBase::Impl>(n_workers);
  for (int i = 0; i < n_workers; ++i) {
    this->_workers.emplace_back(&ContextBase::Impl::run, this->impl.get(), this, i);
  }
}

void Context::stop() {
  this->impl->finished = true;
  for (auto& worker : this->_workers) {
    if (worker.joinable()) {
      worker.join();
    }
  }
}

}  // namespace cgo
