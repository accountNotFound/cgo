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

class TimeoutException : public std::exception {
 public:
  TimeoutException(const std::string& msg) : _msg(msg) {}
  const char* what() const noexcept { return this->_msg.data(); }

 private:
  std::string _msg;
};

template <typename Fn, typename T = std::result_of<Fn()>::type::type>
Coroutine<T> timeout(Fn&& target_builder, unsigned long long timeout_ms) {
  using V = std::conditional_t<std::is_same_v<T, void>, void*, T>;
  using E = std::exception_ptr;

  Channel<std::pair<V, E>> chan(1);
  spawn([](Fn target_builder, Channel<std::pair<V, E>> chan) -> Coroutine<V> {
    if constexpr (std::is_same_v<T, void>) {
      try {
        co_await target_builder();
        chan.send_nowait({nullptr, nullptr});
      } catch (...) {
        chan.send_nowait({nullptr, std::current_exception()});
      }
    } else {
      try {
        T res = co_await target_builder();
        chan.send_nowait({std::move(res), nullptr});
      } catch (...) {
        chan.send_nowait({T(), std::current_exception()});
      }
    }
  }(std::move(target_builder), chan));

  co_await chan.test(timeout_ms);

  std::optional<std::pair<V, E>> opt = chan.recv_nowait();
  if (!opt.has_value()) {
    throw TimeoutException("timeout after " + std::to_string(timeout_ms) + " ms");
  }
  auto& [res, err] = *opt;
  if (err) {
    std::rethrow_exception(err);
  }
  if constexpr (!std::is_same_v<T, void>) {
    co_return std::move(res);
  }
}

template <typename T>
Coroutine<T> timeout(Coroutine<T>&& target, unsigned long long timeout_ms) {
  if constexpr (!std::is_same_v<T, void>) {
    co_return timeout([target = std::move(target)]() { return target; }, timeout_ms);
  }
  co_await timeout([target = std::move(target)]() { return std::move(target); }, timeout_ms);
}

}  // namespace cgo
