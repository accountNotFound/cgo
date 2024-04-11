#pragma once

#include "./event.h"
#include "core/condition.h"

namespace cgo {

// a simple disposable timer now, can not be cancel
class Timer : public ReferenceType {
 public:
  Timer(unsigned long long milli_sec);
  ReadChannel<void*> chan() { return ReadChannel<void*>(this->_chan); }

 private:
  _impl::Fd _fd = 0;
  Channel<void*> _chan;
};

Coroutine<void> sleep(unsigned long long milli_sec);

}  // namespace cgo
