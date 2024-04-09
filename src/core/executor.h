#pragma once

#include <thread>
#include <vector>

#include "./context.h"

namespace cgo::_impl {

class EventHandler;

class TaskExecutor {
 public:
  TaskExecutor(ScheduleContext* context, EventHandler* handler) : _context(context), _handler(handler) {}
  void start(size_t worker_num);
  void stop();

 private:
  bool _finish_flag = false;
  std::vector<std::thread> _workers;
  ScheduleContext* _context;
  EventHandler* _handler;
};

}  // namespace cgo::_impl
