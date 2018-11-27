#include "stdo/server.h"
#include "stdo/ntapi.h"

#include <AclAPI.h>

using namespace stdo;
using namespace stdo::server;

ClientConnectionHandler::ClientConnectionHandler(HANDLE pipe, int clientId)
  noexcept
  : _clientId{clientId}, _callback{connect(pipe)}
{
}

void ClientConnectionHandler::createResponse(const char *header,
                                             std::string_view message)
{
  assert(strlen(header) == 4);
  _buffer.resize(4 + message.length());
  std::memcpy(_buffer.data(), header, 4);
  if (message.length()) {
    std::memcpy(_buffer.data() + 4, message.data(), message.length());
  }
}

ClientConnectionHandler::Callback
ClientConnectionHandler::connect(HANDLE pipe) {
  resetBuffer();
  ConnectNamedPipe(pipe, _overlapped.get());

  switch (GetLastError()) {
  case ERROR_IO_PENDING:
    log::trace("Client {}: scheduling client connection callback.", _clientId);
    _connection = pipe;
    return &ClientConnectionHandler::finishConnect;
  case ERROR_PIPE_CONNECTED:
    log::trace("Client {}: already connected; reading.", _clientId);
    _connection = pipe;
    return finishConnect();
  default:
    log::error("Client {}: ConnectNamedPipe failed: {}", _clientId,
               getLastErrorString());
    return nullptr;
  }
}

ClientConnectionHandler::Callback
ClientConnectionHandler::finishConnect() {
  if (getOverlappedResult(_connection) != ERROR_SUCCESS) {
    log::error("Client {}: error finalizing connection.", _clientId);
    return nullptr;
  }
  return read();
}

ClientConnectionHandler::Callback
ClientConnectionHandler::read() {
  switch (beginRead(_connection)) {
  case EventStatus::Finished:
    log::trace("Client {}: Read ready; responding.", _clientId);
    return finishRead();
  case EventStatus::InProgress:
    log::trace("Client {}: Scheduling async response.", _clientId);
    return &ClientConnectionHandler::finishRead;
  default:
    log::error("Client {}: Read failed.", _clientId);
    return nullptr;
  }
}

ClientConnectionHandler::Callback
ClientConnectionHandler::finishRead() {
  switch (endReadWrite(_connection)) {
  case EventStatus::Finished:
    break;
  case EventStatus::InProgress:
    log::trace("Client {}: More data to read.", _clientId);
    return read();
  case EventStatus::Failed:
    log::error("Client {}: Finalizing read failed.", _clientId);
    return nullptr;
  }
  return respond();
}

ClientConnectionHandler::Callback
ClientConnectionHandler::respond() {
  Callback nextCb = &ClientConnectionHandler::finishRespond<false>;
  if (_buffer.size() < 4) {
    log::warn("Client {}: No message header found.", _clientId);
  } else if (dispatchMessage()) {
    nextCb = &ClientConnectionHandler::finishRespond<true>;
  }

  switch (beginWrite(_connection)) {
  case EventStatus::InProgress:
    log::trace("Client {}: Write in progress.", _clientId);
    break;
  case EventStatus::Failed:
    log::error("Client {}: Write failed.", _clientId);
    return nullptr;
  case EventStatus::Finished:
    log::trace("Client {}: Write finished.", _clientId);
    return nextCb(this);
  }

  return nextCb;
}

template<bool Loop>
ClientConnectionHandler::Callback
ClientConnectionHandler::finishRespond() {
  if (endReadWrite(_connection) != EventStatus::Finished) {
    log::error("Client {}: Finalizing write failed.", _clientId);
    return nullptr;
  }
  if constexpr (Loop) {
    return read();
  } else {
    return reset();
  }
}

template
ClientConnectionHandler::Callback
ClientConnectionHandler::finishRespond<true>();

template
ClientConnectionHandler::Callback
ClientConnectionHandler::finishRespond<false>();

ClientConnectionHandler::Callback
ClientConnectionHandler::reset() {
  _userToken = nullptr;
  log::trace("Client {}: Resetting connection.", _clientId);
  HANDLE pipe = _connection;
  _connection = nullptr;
  return connect(pipe);
}

bool ClientConnectionHandler::dispatchMessage() {
  char header[5];
  std::memcpy(header, _buffer.data(), 4);
  header[4] = 0;
  log::trace("Client {}: Dispatching message {}.", _clientId, header);

  // Check if the header is still the same on exit; that means we forgot
  // to set it.
  STDO_SCOPEEXIT_THIS {
    if (_buffer.size() < 4 || !std::memcmp(_buffer.data(), header, 4)) {
      log::debug("Response was not set!");
      createResponse(msg::server::InternalError, "Unknown error.");
    }
  };

  if (!std::memcmp(header, msg::client::Credential, 4)) {
    // Verify the username/password pair.
    auto bufferEnd = _buffer.cend();
    auto usernameBegin = _buffer.cbegin() + 4;
    auto usernameEnd = usernameBegin;
    while (true) {
      if (usernameEnd == bufferEnd) {
        log::warn("Client {}: Password not found in message.", _clientId);
        createResponse(msg::server::InvalidMessage,
                       "Password missing.");
        return false;
      }
      if (*usernameEnd == 0) {
        break;
      }
      ++usernameEnd;
    }
    auto passwordBegin = usernameEnd + 1;
    auto passwordEnd = passwordBegin;
    // The password shouldn't have any nulls.
    while (true) {
      if (passwordEnd == bufferEnd) {
        break;
      }
      if (*passwordEnd == 0) {
        log::warn("Client {}: Password contains NUL.", _clientId);
        createResponse(msg::server::InvalidMessage,
                       "Incorrect password format.");
        return false;
      }
      ++passwordEnd;
    }
    _buffer.emplace_back(0);
    return tryToLogonUser(&*usernameBegin, &*passwordBegin);
  } else if (!std::memcmp(header, msg::client::Bless, 4)) {
    if (_buffer.size() != 4 + sizeof(HANDLE)) {
      log::warn("Client {}: Invalid bless message.", _clientId);
      createResponse(msg::server::InvalidMessage);
    } else {
      HANDLE remoteHandle;
      std::memcpy(&remoteHandle, _buffer.data() + 4, sizeof(HANDLE));
      if (bless(remoteHandle)) {
        createResponse(msg::server::Success);
      } else {
        createResponse(msg::server::InternalError,
                       "Token substitution failed.");
      }
    }
    return false;
  } else {
    log::warn("Client {}: Unknown message header {}.", _clientId, header);
    createResponse(msg::server::InvalidMessage, "Unknown message header");
    return false;
  }
}

bool ClientConnectionHandler::tryToLogonUser(const char *username,
                                             const char *password)
{
  // FIXME: Yeah...
  if (std::memcmp(password, "password", 9)) {
    log::trace("Client {}: Access denied - invalid password.", _clientId);
    createResponse(msg::server::AccessDenied);
    return false;
  }

  createResponse(msg::server::InternalError);

  HObject clientProcess;
  ULONG processId;
  if (!GetNamedPipeClientProcessId(_connection, &processId)) {
    log::error("Client {}: Couldn't get client process ID: {}", _clientId,
               getLastErrorString());
    return false;
  }
  auto const access =
    PROCESS_DUP_HANDLE | PROCESS_VM_READ | PROCESS_QUERY_INFORMATION;
  if (!(clientProcess = OpenProcess(access, false, processId))) {
    log::error("Client {}: Couldn't open client process: {}", _clientId,
               getLastErrorString());
    return false;
  }

  HObject currentToken;
  // if (!OpenProcessToken(clientProcess, TOKEN_DUPLICATE | TOKEN_READ,
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_READ,
                        &currentToken))
  {
    log::error("Client {}: Couldn't open client process token: {}", _clientId,
               getLastErrorString());
    return false;
  }

  PSID ownerSid;
  PSID groupSid;
  PACL dacl;
  PACL sacl;
  PSECURITY_DESCRIPTOR secDesc;
  if (!SUCCEEDED(GetSecurityInfo(currentToken, SE_KERNEL_OBJECT,
                                 DACL_SECURITY_INFORMATION |
                                   SACL_SECURITY_INFORMATION |
                                   GROUP_SECURITY_INFORMATION |
                                   OWNER_SECURITY_INFORMATION,
                                 &ownerSid, &groupSid, &dacl, &sacl,
                                 &secDesc)))
  {
    log::error("Client {}: Couldn't get security info: {}", _clientId,
               getLastErrorString());
    return false;
  }
  STDO_SCOPEEXIT { LocalFree(secDesc); };

  SECURITY_ATTRIBUTES secAttr;
  secAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
  secAttr.bInheritHandle = true;
  secAttr.lpSecurityDescriptor = secDesc;

  //

  HObject newToken;
  if (!DuplicateTokenEx(currentToken, MAXIMUM_ALLOWED, &secAttr,
                        SecurityImpersonation, TokenPrimary, &newToken))
  {
    log::error("Client {}: Couldn't duplicate token: {}", _clientId,
               getLastErrorString());
    return false;
  }

  _userToken = std::move(newToken);
  log::trace("Client {}: Stored new token.", _clientId);

  createResponse(msg::server::Success);
  return true;
}

bool ClientConnectionHandler::bless(HANDLE remoteHandle) {
  HObject clientProcess;
  HObject localHandle;
  ULONG processId;
  if (!_userToken) {
    log::error("Client {}: Not authenticated.", _clientId);
    return false;
  }

  if (!GetNamedPipeClientProcessId(_connection, &processId)) {
    log::error("Client {}: Couldn't get client process ID: {}", _clientId,
               getLastErrorString());
    return false;
  }
  auto const access = PROCESS_DUP_HANDLE | PROCESS_VM_READ;
  if (!(clientProcess = OpenProcess(access, false, processId))) {
    log::error("Client {}: Couldn't open client process: {}", _clientId,
               getLastErrorString());
    return false;
  }
  log::trace("Trying to duplicate remote handle 0x{:X}",
             reinterpret_cast<size_t>(remoteHandle));
  if (!DuplicateHandle(clientProcess, remoteHandle, GetCurrentProcess(),
                       &localHandle, PROCESS_SET_INFORMATION, false, 0))
  {
    log::error("Client {}: Couldn't duplicate remote handle: {}", _clientId,
               getLastErrorString());
    return false;
  }

  auto ntdll = LinkedModule{L"ntdll.dll"};
  auto NtSetInformationProcess =
    ntdll.get<nt::NtSetInformationProcess_t>("NtSetInformationProcess");

  nt::PROCESS_ACCESS_TOKEN processAccessToken{_userToken, nullptr};
  if (!NT_SUCCESS(NtSetInformationProcess(localHandle,
                                          nt::ProcessAccessToken,
                                          &processAccessToken,
                                          sizeof(nt::PROCESS_ACCESS_TOKEN))))
  {
    log::error("Client {}: Couldn't assign access token: {}", _clientId,
               getLastErrorString());
    return false;
  }

  log::trace("Client {}: Adjusted remote process token: {}", _clientId,
             getLastErrorString());

  return true;
}
