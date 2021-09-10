#ifndef WSUDO_SERVER_H
#define WSUDO_SERVER_H

#include "wsudo.h"
#include "events.h"
#include "session.h"

#include "corio.h"
#include "wscoro/basictasks.h"

#include <memory>
#include <type_traits>
#include <vector>
#include <string_view>
#include <AclAPI.h>

#include <wil/resource.h>

namespace wsudo::server {

// Server status codes
enum Status : int {
  StatusUnset = -1,
  StatusOk = 0,
  StatusCreatePipeFailed,
  StatusTimedOut,
  StatusEventFailed,
};

inline const char *statusToString(Status status) {
  switch (status) {
    default: return "unknown status";
    case StatusUnset: return "status not set";
    case StatusOk: return "ok";
    case StatusCreatePipeFailed: return "pipe creation failed";
    case StatusTimedOut: return "timed out";
    case StatusEventFailed: return "event failed";
  }
}

// Creates connections to a named pipe with the necessary security attributes.
class NamedPipeHandleFactory final {
public:
  explicit NamedPipeHandleFactory(LPCWSTR pipeName) noexcept;

  // Create a new pipe connection.
  HObject operator()();

  // Returns true if initialization succeeded and a connection can be created.
  bool good() const;

  //
  explicit operator bool() const
  { return good(); }

private:
  bool _firstInstance = true;
  LPCWSTR _pipeName;
  SID_IDENTIFIER_AUTHORITY _sidAuth;
  Handle<PSID, FreeSid> _sid;
  EXPLICIT_ACCESS_W _explicitAccess;
  HLocalPtr<PACL> _acl;
  HLocalPtr<PSECURITY_DESCRIPTOR> _securityDescriptor;
  SECURITY_ATTRIBUTES _securityAttributes;
};

class ClientConnectionHandler : public events::EventOverlappedIO {
public:
  using Self = ClientConnectionHandler;
  using Callback = recursive_mem_callback<Self>;

  explicit ClientConnectionHandler(HObject pipe, int clientId,
                                   session::SessionManager &sessionManager)
                                   noexcept;

  bool reset() override;

  events::EventStatus operator()(events::EventListener &) override;

protected:
  HANDLE fileHandle() const override {
    return _pipe;
  }

private:
  HObject _pipe;
  int _clientId;
  session::SessionManager &_sessionManager;
  Callback _callback;
  HObject _userToken{};

  void createResponse(const char *header,
                      std::string_view message = std::string_view{});

  Callback beginConnect();
  Callback endConnect();
  Callback read();
  Callback respond();
  Callback resetConnection();

  // Returns true to read another message, false to reset the connection.
  bool dispatchMessage();
  bool tryToLogonUser(char *username, char *password);
  bool bless(HANDLE remoteHandle);
};

// Server configuration.
struct Config {
  // Named pipe filename.
  std::wstring pipeName;

  // Pointer to global quit event handle.
  HANDLE *quitEvent;

  // Server status return value.
  Status status = StatusUnset;

  explicit Config(std::wstring pipeName, HANDLE *quitEvent)
    : pipeName(std::move(pipeName)), quitEvent(quitEvent)
  {}
};

// Run the server. This function will continue until an error occurs or the
// quit event is triggered. After it returns, config.status will be set to
// the server's exit status.
void serverMain(Config &config);

} // namespace wsudo::server

namespace wsudo {

using namespace server;

class Server {
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

  class Connection;
  wscoro::Task<bool> run(Connection &conn);

  class Connection : private CorIO::AsyncFile {
    Server *_server;
    std::string _buffer;

    wscoro::Task<bool> connect();
    bool disconnect();

  public:
    explicit Connection(CorIO &corio, wil::unique_hfile file, Server &server)
      : CorIO::AsyncFile::AsyncFile(corio, std::move(file)),
        _server{&server}
    {}

    wscoro::FireAndForget run();

    // Writes zeroes to the buffer before clearing.
    void clear() noexcept {
      _buffer.assign(_buffer.size(), '\0');
      _buffer.clear();
    }
    std::string &buffer() noexcept { return _buffer; }
    std::string &append(std::string_view str) { return _buffer.append(str); }

    wscoro::Task<bool> read();
    wscoro::Task<bool> respond();
  };
};

} // namespace wsudo

#endif // WSUDO_SERVER_H
