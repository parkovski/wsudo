#ifndef WSUDO_SERVER_H
#define WSUDO_SERVER_H

#include "wsudo.h"
#include "corio.h"

#include <memory>
#include <type_traits>
#include <vector>
#include <string_view>

#include <AclAPI.h>

#include <wil/resource.h>

namespace wsudo {

class Server {
public:
  class Connection;

private:
  // Security
  wil::unique_sid _sid;
  wil::unique_hlocal_ptr<ACL> _acl;
  wil::unique_hlocal_ptr<SECURITY_DESCRIPTOR> _securityDescriptor;
  SECURITY_ATTRIBUTES _securityAttributes;

  std::wstring _pipeName;

  int _activeConnections = 0;
  void *_quitHandle = nullptr;

  HRESULT initSecurity() noexcept;
  HANDLE initPipe(bool first = false);

public:
  explicit Server(std::wstring pipeName);

  HRESULT operator()(int nUserThreads = 0, int nSystemThreads = 0);
  void quit();

  wscoro::Task<bool> run(Connection &conn);

private:
  wscoro::Task<bool> dispatch(Connection &conn);

public:
  class Connection : private CorIO::Pipe {
    Server *_server;
    std::string _buffer;

    wscoro::Task<bool> connect();
    bool disconnect();

  public:
    explicit Connection(CorIO &corio, wil::unique_hfile file, Server &server)
      : CorIO::Pipe::Pipe(corio, std::move(file)),
        _server{&server}
    {}

    wscoro::FireAndForget run();

    // Writes zeroes to the buffer before clearing.
    std::string &clear() noexcept {
      _buffer.assign(_buffer.size(), '\0');
      _buffer.clear();
      return _buffer;
    }
    std::string &buffer() noexcept { return _buffer; }
    std::string &append(std::string_view str) { return _buffer.append(str); }

    wscoro::Task<bool> read();
    wscoro::Task<bool> respond();
  };
};

} // namespace wsudo

#endif // WSUDO_SERVER_H
