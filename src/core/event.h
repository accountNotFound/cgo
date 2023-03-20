#pragma once

#include "channel.h"

namespace cgo::impl {

using Fd = size_t;

class Event {
 public:
  enum Type { IN = 0x1, OUT = 0x2, ERR = 0x4, ONESHOT = 0x8 };

 public:
  Event(Fd fd, Type types) : _fd(fd), _types(types), _chan(1) {}
  auto fd() const -> Fd { return this->_fd; }
  auto types() const -> Type { return this->_types; }
  auto chan() const -> Channel<bool> { return this->_chan; }

 private:
  const Fd _fd;
  const Type _types;
  Channel<bool> _chan;
};

extern Event::Type operator|(Event::Type a, Event::Type b);

class EventHandler {
 public:
  EventHandler(size_t fd_capacity);
  EventHandler(const EventHandler&) = delete;
  ~EventHandler();
  void add(Fd fd, Event& on);
  void mod(Fd fd, Event& on);
  void del(Fd fd, Event& on);
  auto handle(size_t handle_batch = 128, size_t timeout_ms = 50) -> size_t;

#if defined(linux) || defined(__linux) || defined(__linux__)
 private:
  Fd _handler_fd;
#endif
};

}  // namespace cgo::impl
