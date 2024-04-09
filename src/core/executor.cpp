#include "./executor.h"

#include "aio/event.h"

namespace cgo::_impl {

thread_local ScheduleContext* ScheduleContext::current = nullptr;
thread_local ScheduleTask* ScheduleTask::current = nullptr;
thread_local EventHandler* EventHandler::current = nullptr;

void TaskExecutor::start(size_t worker_num) {
  for (int i = 0; i < worker_num; i++) {
    this->_workers.emplace_back(std::thread([this]() {
      ScheduleContext::current = this->_context;
      EventHandler::current = this->_handler;

      while (!this->_finish_flag) {
        ScheduleTask* current = this->_context->get();
        if (!current) {
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
          continue;
        } else {
          current->schedule_callback = nullptr;
        }

        ScheduleTask::current->current = current;
        while (!current->done() && !current->schedule_callback) {
          current->resume();
        }
        if (current->done()) {
          this->_context->destroy(current);
        } else {
          current->schedule_callback();
        }
        ScheduleTask::current = nullptr;
      }
    }));
  }
}

void TaskExecutor::stop() {
  this->_finish_flag = true;
  for (auto& w : this->_workers) {
    if (w.joinable()) {
      const_cast<std::thread&>(w).join();
    }
  }
  this->_workers.clear();
}

}  // namespace cgo::_impl
