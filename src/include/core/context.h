#pragma once

#include <thread>

#include "core/schedule.h"

namespace cgo {

namespace _impl::_ctx {

struct ContextBase {
 public:
  ContextBase();

  virtual ~ContextBase();

  struct Impl;
  std::unique_ptr<Impl> impl;
};

}  // namespace _impl::_ctx

class Context : public _impl::_ctx::ContextBase {
 public:
  Context() = default;

  Context(const Context&) = delete;

  Context(Context&&) = delete;

  void start(size_t n_workers);

  void stop();

 private:
  using _impl::_ctx::ContextBase::Impl;
  using _impl::_ctx::ContextBase::impl;
  std::vector<std::thread> _workers;
};

}  // namespace cgo
