#pragma once

#include "aio/atime.h"
#include "aio/socket.h"
#include "core/condition.h"
#include "core/context.h"
#include "core/coroutine.h"
#include "core/executor.h"
#include "util/format.h"

namespace cgo {

class Context {
 public:
  Context() : _sched_ctx(), _event_hdlr(), _executor(&_sched_ctx, &_event_hdlr) {}
  Context(const Context&) = delete;
  Context(Context&&) = delete;

  void start(size_t exec_num) { this->_executor.start(exec_num); }
  void stop() { this->_executor.stop(); }
  void loop(const std::function<bool()>& flag) {
    while (flag()) {
      this->_event_hdlr.handle();
    }
  }

 private:
  _impl::ScheduleContext _sched_ctx;
  _impl::EventHandler _event_hdlr;
  _impl::TaskExecutor _executor;
};

class Defer {
 public:
  Defer(std::function<void()>&& callback) : _callback(callback) {}
  ~Defer() { this->_callback(); }

 private:
  std::function<void()> _callback;
};

}  // namespace cgo
