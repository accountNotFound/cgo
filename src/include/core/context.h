#pragma once

#include <thread>

#include "core/event.h"
#include "core/schedule.h"
#include "core/timed.h"

namespace cgo {

class Context {
  friend class _impl::SchedContext;
  friend class _impl::TimedContext;
  friend class _impl::EventContext;

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
  std::unique_ptr<_impl::EventContext> _event_ctx = nullptr;
  bool _finished = false;

  void _run(size_t pindex);
};

}  // namespace cgo
