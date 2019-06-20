#include "wsudo/events.h"
#include "wsudo/wsudo.h"

using namespace wsudo;
using namespace wsudo::events;

// {{{ EventHandler

EventHandler::~EventHandler() {
}

bool EventHandler::reset() {
  // By default, event handlers cannot be reused. Subclasses must opt in to
  // this behavior.
  return false;
}

// }}} EventHandler

// {{{ EventListener

EventStatus EventListener::next(DWORD timeout) {
  log::trace("Waiting on {} events.", _events.size());

  if (_events.size() == 0) {
    return EventStatus::Finished;
  }

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
      break;
    case EventStatus::Finished:
      if (_handlers[index]->reset()) {
        log::trace("Event #{} returned Finished and was reset.", index);
      } else {
        log::debug("Event #{} returned Finished and will be removed.", index);
        remove(index);
      }
      break;
    case EventStatus::Failed:
      if (_handlers[index]->reset()) {
        log::warn("Event #{} returned Failed, but reset succeeded.", index);
      } else {
        log::error("Event #{} returned Failed.", index);
        remove(index);
      }
      break;
    }
  } else if (waitResult >= WAIT_ABANDONED_0 &&
             waitResult < WAIT_ABANDONED_0 + _events.size())

  {
    size_t index = (size_t)(waitResult - WAIT_ABANDONED_0);
    log::error("Mutex abandoned state signaled for handler #{}.", index);
    remove(index);
  } else if (waitResult == WAIT_FAILED) {
    log::critical("WaitForMultipleObjects failed: {}",
                  lastErrorString());
    return EventStatus::Failed;
  } else {
    log::critical("WaitForMultipleObjects returned 0x{:X}: {}", waitResult,
                  lastErrorString());
    return EventStatus::Failed;
  }

  return _events.size() > 0 ? EventStatus::Ok : EventStatus::Finished;
}

EventStatus EventListener::run(DWORD timeout) {
  _running = true;

  auto status = EventStatus::Finished;
  while (_running) {
    status = next(timeout);
    if (status != EventStatus::Ok) {
      break;
    }
  }

  return status;
}

void EventListener::remove(size_t index) {
  if (index >= _handlers.size()) {
    log::error("Event index {} out of range.", index);
    return;
  }

  assert(_handlers.size() == _events.size());

  _events.erase(_events.cbegin() + index);
  _handlers.erase(_handlers.cbegin() + index);
}

// }}} EventListener
