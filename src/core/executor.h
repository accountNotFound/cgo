#pragma once

#include <thread>
#include <vector>

#include "./scheduler.h"
#include "./timer.h"

namespace cgo::_impl {

class Executor {
 public:
  Executor(Scheduler& scheduler, Timer& timer) : _scheduler(&scheduler), _timer(&timer) {}
  void start(size_t worker_num);
  void stop();

 private:
  bool _finish_flag = false;
  std::vector<std::thread> _workers;
  Scheduler* _scheduler;
  Timer* _timer;
};

}  // namespace cgo::_impl
