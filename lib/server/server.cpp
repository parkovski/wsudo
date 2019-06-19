#include "wsudo/server.h"
#include "wsudo/session.h"

#pragma comment(lib, "Advapi32.lib")

using namespace wsudo;
using namespace wsudo::server;

void wsudo::server::serverMain(Config &config) {
  using namespace events;

  session::SessionManager sessionManager{60 * 10};

  NamedPipeHandleFactory pipeHandleFactory{config.pipeName.c_str()};
  if (!pipeHandleFactory) {
    config.status = StatusCreatePipeFailed;
    return;
  }

  EventListener listener;
  *config.quitEvent = CreateEventW(nullptr, true, false, nullptr);
  listener.emplace(*config.quitEvent, [](EventListener &listener) {
    listener.stop();
    return EventStatus::Finished;
  });

  for (int id = 1; id <= MaxPipeConnections; ++id) {
    listener.emplace<ClientConnectionHandler>(pipeHandleFactory(), id,
                                              sessionManager);
  }

  EventStatus status = listener.run();

  if (status == EventStatus::Failed) {
    config.status = StatusEventFailed;
  } else {
    config.status = StatusOk;
  }
}

