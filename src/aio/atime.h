#include "./event.h"
#include "core/condition.h"

namespace cgo {

class Timer {
 public:
  Timer(unsigned long long milli_sec);
  ~Timer();
  ReadChannel<void*> chan() { return this->_chan; }

 private:
  _impl::Fd _fd;
  Channel<void*> _chan;
};

Coroutine<void> sleep(unsigned long long milli_sec);

}  // namespace cgo
