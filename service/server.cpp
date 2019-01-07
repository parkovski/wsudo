#include "wsudo/server.h"
#include "wsudo/events.h"
#include "wsudo/ntapi.h"

#pragma comment(lib, "Advapi32.lib")

using namespace wsudo;
using namespace wsudo::server;

void wsudo::server::serverMain(Config &config) {
  using namespace events;

  NamedPipeHandleFactory pipeHandleFactory{config.pipeName.c_str()};
  if (!pipeHandleFactory) {
    config.status = StatusCreatePipeFailed;
    return;
  }

  EventListener listener;
  *config.quitEvent = listener.emplace_back(EventCallback {
    [](EventListener &listener) {
      listener.stop();
      return EventStatus::Finished;
    }
  }).event();

  //listener.emplace_back(ClientConnectionHandler{pipeHandleFactory(), 1});

  EventStatus status;
  do {
    status = listener.run();
  } while (status == EventStatus::Ok);

  if (status == EventStatus::Failed) {
    config.status = StatusEventFailed;
  } else if (status == EventStatus::Finished) {
    config.status = StatusOk;
  }
}

