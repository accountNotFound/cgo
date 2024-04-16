#pragma once

#include <thread>
#include <vector>

#include "./scheduler.h"
#include "./timer.h"

namespace cgo::_impl {

class EventHandler;

class Executor {
 public:
  Executor(Scheduler& scheduler, TimeHandler& time_handler, EventHandler& event_handler)
      : _scheduler(&scheduler), _time_handler(&time_handler), _event_handler(&event_handler) {}
  void start(size_t worker_num);
  void stop();

 private:
  bool _finish_flag = false;
  std::vector<std::thread> _workers;
  Scheduler* _scheduler;
  TimeHandler* _time_handler;
  EventHandler* _event_handler;
};

}  // namespace cgo::_impl
