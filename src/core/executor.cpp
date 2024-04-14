#include "./executor.h"

namespace cgo::_impl {

thread_local Scheduler* Scheduler::current = nullptr;
thread_local Task* Task::current = nullptr;
thread_local Timer* Timer::current = nullptr;

void Executor::start(size_t worker_num) {
  for (int i = 0; i < worker_num; i++) {
    this->_workers.emplace_back(std::thread([this]() {
      Scheduler::current = this->_scheduler;
      Timer::current = this->_timer;

      while (!this->_finish_flag) {
        Task* current = this->_scheduler->get();
        if (!current) {
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
          continue;
        } else {
          current->schedule_callback = nullptr;
        }

        Task::current = current;
        while (!current->done() && !current->schedule_callback) {
          current->resume();
        }
        if (current->done()) {
          this->_scheduler->destroy(current);
        } else {
          current->schedule_callback();
        }
        Task::current = nullptr;
      }
    }));
  }
}

void Executor::stop() {
  this->_finish_flag = true;
  for (auto& w : this->_workers) {
    if (w.joinable()) {
      const_cast<std::thread&>(w).join();
    }
  }
  this->_workers.clear();
}

}  // namespace cgo::_impl
