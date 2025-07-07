#pragma once

#include <thread>

#include "core/schedule.h"
#include "core/timer.h"

namespace cgo {

class Context {
  friend class _impl::SchedContext;
  friend class _impl::TimedContext;

 public:
  Context() = default;

  Context(const Context&) = delete;

  Context(Context&&) = delete;

  void start(size_t n_worker);

  void stop();

  bool closed() const { return _finished; }

 private:
  std::vector<std::thread> _workers;
  std::unique_ptr<_impl::SchedContext> _sched_ctx = nullptr;
  std::unique_ptr<_impl::TimedContext> _timed_ctx = nullptr;
  bool _finished = false;

  void _run(size_t pindex);
};

}  // namespace cgo
