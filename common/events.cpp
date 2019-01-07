#include "wsudo/events.h"
#include "wsudo/wsudo.h"

using namespace wsudo;
using namespace wsudo::events;

// {{{ EventHandler

EventHandler::~EventHandler() {
}

bool EventHandler::reset() {
  return false;
}

// }}} EventHandler

// {{{ EventListener

EventStatus EventListener::next(DWORD timeout) {
  log::trace("Waiting on {} events", _events.size());

  auto waitResult = WaitForMultipleObjects(
    static_cast<DWORD>(_events.size()), &_events[0], false, timeout
  );

  if (waitResult == WAIT_TIMEOUT) {
    log::error("WaitForMultipleObjects timed out.");
    return EventStatus::Failed;
  } else if (waitResult >= WAIT_OBJECT_0 &&
             waitResult < WAIT_OBJECT_0 + _events.size())
  {
    size_t index = static_cast<size_t>(waitResult - WAIT_OBJECT_0);
    log::trace("Event #{} signaled.", index);

    switch ((*_handlers[index])(*this)) {
    case EventStatus::Ok:
      log::trace("Event #{} returned Ok.", index);
      ResetEvent(_events[index]);
      break;
    case EventStatus::Finished:
      log::trace("Event #{} returned Finished.", index);
      if (!_handlers[index]->reset()) {
        remove(index);
      }
      break;
    case EventStatus::Failed:
      log::error("Event #{} returned Failed.", index);
      remove(index);
      break;
    }
  } else if (waitResult >= WAIT_ABANDONED_0 &&
             waitResult < WAIT_ABANDONED_0 + _events.size())

  {
    size_t index = (size_t)(waitResult - WAIT_ABANDONED_0);
    log::error("Mutex abandoned state signaled for handler #{}.", index);
    remove(index);
    return EventStatus::Failed;
  } else if (waitResult == WAIT_FAILED) {
    log::critical("WaitForMultipleObjects failed: {}",
                  lastErrorString());
    return EventStatus::Failed;
  } else {
    log::critical("WaitForMultipleObjects returned 0x{:X}: ", waitResult,
                  lastErrorString());
    return EventStatus::Failed;
  }

  return EventStatus::Ok;
}

EventStatus EventListener::run(DWORD timeout) {
  _running = true;

  do {
    auto status = next(timeout);
    if (status != EventStatus::Ok) {
      return status;
    }
  } while (isRunning());

  return EventStatus::Ok;
}

void EventListener::remove(size_t index) {
  assert(index < _handlers.size());

  _events.erase(_events.cbegin() + index);
  _handlers.erase(_handlers.cbegin() + index);
}

// }}} EventListener
