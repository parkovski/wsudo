#ifndef WSUDO_SERVER_H
#define WSUDO_SERVER_H

#include "wsudo.h"
#include "winsupport.h"
#include "events.h"

#include <memory>
#include <type_traits>
#include <vector>
#include <string_view>
#include <AclAPI.h>

namespace wsudo::server {

// Maximum concurrent server connections. Being sudo, it's unlikely to have to
// process many things concurrently, but we have to give Windows a number.
constexpr int MaxPipeConnections = 10;

// Pipe timeout, again for Windows.
constexpr int PipeDefaultTimeout = 0;

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
  explicit operator bool() const;

private:
  LPCWSTR _pipeName;
  SID_IDENTIFIER_AUTHORITY _sidAuth;
  Handle<PSID, FreeSid> _sid;
  EXPLICIT_ACCESS_W _explicitAccess;
  Handle<PACL, LocalFree> _acl;
  Handle<PSECURITY_DESCRIPTOR, LocalFree> _securityDescriptor;
  SECURITY_ATTRIBUTES _securityAttributes;
};

class ClientConnectionHandler : public events::EventOverlappedIO {
public:
  using Self = ClientConnectionHandler;
  using Callback = recursive_mem_callback<Self>;

  explicit ClientConnectionHandler(HObject pipe, int clientId) noexcept;

  bool reset() override;

  events::EventStatus operator()(events::EventListener &) override;

protected:
  HANDLE fileHandle() const override {
    return _pipe;
  }

private:
  HObject _pipe;
  int _clientId;
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
  bool tryToLogonUser(const char *username, const char *password);
  bool bless(HANDLE remoteHandle);
};

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

void serverMain(Config &config);

} // namespace wsudo::server

#endif // WSUDO_SERVER_H

