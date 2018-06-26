#ifndef STDO_SERVER_H
#define STDO_SERVER_H

#include "stdo/winsupport.h"

#include <atomic>

namespace stdo::server {

constexpr int MaxPipeConnections = 5;
constexpr int PipeInBufferSize = 1024;
constexpr int PipeOutBufferSize = 256;
constexpr int PipeDefaultTimeout = 0;

enum Status : int {
  StatusUnset = -1,
  StatusOk = 0,
  StatusCreatePipeFailed,
  StatusTimedOut,
  StatusEventFailed,
};

inline const char *statusToString(Status status) {
  switch (status) {
  case StatusUnset: return "status not set";
  case StatusOk: return "ok";
  case StatusCreatePipeFailed: return "pipe creation failed";
  case StatusTimedOut: return "timed out";
  default: return "unknown error";
  }
}

struct Config {
  std::wstring pipeName;
  Status status = StatusUnset;
  HObject quitEvent;

  Config(std::wstring pipeName, HObject quitEvent)
    : pipeName(std::move(pipeName)),
      quitEvent(std::move(quitEvent))
  {}

  void quit() {
    SetEvent(quitEvent);
  }
};

void serverMain(Config &config);

} // namespace stdo::server

#endif // STDO_SERVER_H
