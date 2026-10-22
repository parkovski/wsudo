#ifndef WSUDO_SERVER_H
#define WSUDO_SERVER_H

#include "corio.h"
#include "session.h"
#include "message.h"
#include "config.h"

#include <string_view>

#include <AclAPI.h>

#include <wil/resource.h>

namespace wsudo {

HRESULT installService();
bool uninstallService();
HRESULT clearEventLog();

extern bool g_isService;
extern class Server *g_server;

extern SERVICE_STATUS g_serviceStatus;
extern SERVICE_STATUS_HANDLE g_serviceStatusHandle;

class Server {
public:
  class Connection;

  constexpr static int UserThreads = 1;
  constexpr static int SystemThreads = 1;

private:
  // Security
  wil::unique_sid _sid;
  wil::unique_hlocal_ptr<ACL> _acl;
  wil::unique_hlocal_ptr<SECURITY_DESCRIPTOR> _securityDescriptor;
  SECURITY_ATTRIBUTES _securityAttributes;

  std::wstring _pipeName;

  int _activeConnections = 0;
  void *_quitHandle = nullptr;

  SessionManager _sessionManager;

  HRESULT initSecurity() noexcept;
  HANDLE initPipe(bool first = false);

public:
  explicit Server(std::wstring pipeName, ServerConfig &config);

  int operator()(int nUserThreads = 0, int nSystemThreads = 0);
  void quit(int exitCode = 0);

  wscoro::task<> dispatch(Connection &conn);

  class Connection : private CorIO::Pipe {
    Server *_server;
    std::string _buffer;

    wscoro::task<bool> connect();
    bool disconnect();

  public:
    explicit Connection(CorIO &corio, wil::unique_hfile file, Server &server)
      : CorIO::Pipe::Pipe(corio, std::move(file)),
        _server{&server}
    {}

    using Pipe::clientProcessId;

    wscoro::fire_and_forget run();

    // Writes zeroes to the buffer before clearing.
    std::string &clear() noexcept {
      _buffer.assign(_buffer.size(), '\0');
      _buffer.clear();
      return _buffer;
    }
    std::string &buffer() noexcept { return _buffer; }
    std::string &append(std::string_view str) { return _buffer.append(str); }

    wscoro::task<> send(const Message &message);
    wscoro::task<Message> recv();
  };
};

} // namespace wsudo

#endif // WSUDO_SERVER_H
