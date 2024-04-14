#pragma once

#include <thread>
#include <vector>

#include "./scheduler.h"

namespace cgo::_impl {

class Executor {
 public:
  Executor(Scheduler* scheduler) : _scheduler(scheduler) {}
  void start(size_t worker_num);
  void stop();

 private:
  bool _finish_flag = false;
  std::vector<std::thread> _workers;
  Scheduler* _scheduler;
};

}  // namespace cgo::_impl
