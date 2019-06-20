#include "wsudo/server.h"

#include <AclAPI.h>

using namespace wsudo;
using namespace wsudo::server;
using namespace wsudo::events;

ClientConnectionHandler::ClientConnectionHandler(
  HObject pipe, int clientId, session::SessionManager &sessionManager
) noexcept
  : EventOverlappedIO{true},
    _pipe{std::move(pipe)},
    _clientId{clientId},
    _sessionManager{sessionManager},
    _callback{&Self::beginConnect}
{
}

bool ClientConnectionHandler::reset() {
  EventOverlappedIO::reset();

  _userToken = nullptr;
  if (!DisconnectNamedPipe(_pipe) &&
      GetLastError() != ERROR_PIPE_NOT_CONNECTED)
  {
    return false;
  }
  log::debug("Client {}: Resetting connection.", _clientId);
  _callback = &Self::beginConnect;
  SetEvent(_overlapped.hEvent);
  return true;
}

EventStatus ClientConnectionHandler::operator()(EventListener &listener) {
  // First see if there is any overlapped IO to process.
  switch (EventOverlappedIO::operator()(listener)) {
    case EventStatus::Finished:
      break;
    case EventStatus::Failed:
      return EventStatus::Failed;
    case EventStatus::Ok:
      return EventStatus::Ok;
  }

  // No IO, so move on to the next step.
  if (!_callback || !_callback.call_and_swap(*this)) {
    return EventStatus::Failed;
  }
  return EventStatus::Ok;
}

void ClientConnectionHandler::createResponse(const char *header,
                                             std::string_view message)
{
  assert(strlen(header) == 4);
  size_t totalSize = 4 + message.length();
  if (_buffer.size() < totalSize) {
    _buffer.resize(totalSize);
  }
  std::memcpy(_buffer.data(), header, 4);
  if (message.length()) {
    std::memcpy(_buffer.data() + 4, message.data(), message.length());
  }
}

ClientConnectionHandler::Callback
ClientConnectionHandler::beginConnect() {
  if (ConnectNamedPipe(_pipe, &_overlapped)) {
    log::trace("Client {}: connected.", _clientId);
    return read();
  }

  switch (GetLastError()) {
  case ERROR_IO_PENDING:
    log::trace("Client {}: waiting for connection.", _clientId);
    return &Self::endConnect;
  case ERROR_PIPE_CONNECTED:
    log::trace("Client {}: already connected; reading.", _clientId);
    return endConnect();
  default:
    log::error("Client {}: ConnectNamedPipe failed: {}", _clientId,
               lastErrorString());
    return nullptr;
  }
}

ClientConnectionHandler::Callback
ClientConnectionHandler::endConnect() {
  DWORD dummyBytesTransferred;
  if (!GetOverlappedResult(_pipe, &_overlapped,
                           &dummyBytesTransferred, false))
  {
    if (GetLastError() == ERROR_BROKEN_PIPE) {
      log::info("Client {}: connection ended by client.", _clientId);
      return nullptr;
    }
    log::error("Client {}: error finalizing connection: {}", _clientId,
               lastErrorString());
    return nullptr;
  }
  return read();
}

ClientConnectionHandler::Callback
ClientConnectionHandler::read() {
  switch (readToBuffer()) {
    case EventStatus::Failed:
      return nullptr;
    case EventStatus::Finished:
      return respond();
    case EventStatus::Ok:
      return &Self::respond;
    default:
      WSUDO_UNREACHABLE("Invalid EventStatus");
  }
}

ClientConnectionHandler::Callback
ClientConnectionHandler::respond() {
  Callback nextCb = &Self::resetConnection;
  if (dispatchMessage()) {
    nextCb = &Self::read;
  }

  switch (writeFromBuffer()) {
    case EventStatus::Ok:
      return nextCb;
    case EventStatus::Failed:
      return nullptr;
    case EventStatus::Finished:
      return nextCb(*this);
    default:
      WSUDO_UNREACHABLE("Invalid EventStatus");
  }

  return nextCb;
}

ClientConnectionHandler::Callback
ClientConnectionHandler::resetConnection() {
  if (!reset()) {
    log::error("Client {}: Reset failed.", _clientId);
    return nullptr;
  }
  return &Self::beginConnect;
}

bool ClientConnectionHandler::dispatchMessage() {
  if (_buffer.size() < 4) {
    log::warn("Client {}: No message header found.", _clientId);
    createResponse(msg::server::InvalidMessage, "No message header present");
    return false;
  }

  char header[5];
  std::memcpy(header, _buffer.data(), 4);
  header[4] = 0;
  log::debug("Client {}: Dispatching message '{}'.", _clientId, header);

  // Check if the header is still the same on exit; that means we forgot
  // to set it.
  WSUDO_SCOPEEXIT_THIS {
    if (_buffer.size() < 4 || !std::memcmp(_buffer.data(), header, 4)) {
      log::debug("Response was not set!");
      createResponse(msg::server::InternalError);
    }
  };

  if (!std::memcmp(header, msg::client::Credential, 4)) {
    // Verify the username/password pair.
    auto bufferEnd = _buffer.end();
    auto usernameBegin = _buffer.begin() + 4;
    auto usernameEnd = usernameBegin;
    while (true) {
      if (usernameEnd >= bufferEnd - 1) {
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
    return tryToLogonUser(reinterpret_cast<char *>(&*usernameBegin),
                          reinterpret_cast<char *>(&*passwordBegin));
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
    log::warn("Client {}: Unknown message header (0x{:2X}_{:2X}_{:2X}_{:2X}).",
              _clientId, header[0], header[1], header[2], header[3]);
    createResponse(msg::server::InvalidMessage, "Unknown message header");
    return false;
  }
}

bool ClientConnectionHandler::tryToLogonUser(char *username, char *password) {
  auto username_w = to_utf16(username);
  auto password_w = to_utf16(password);
  // Zero the password from memory.
  while (*password) {
    *password++ = 0;
  }

  auto session = _sessionManager.find(username_w);
  if (!session) {
    session = _sessionManager.create(username_w, L"", std::move(password_w));
    if (!session) {
      log::warn("Client {}: Access denied for user '{}'.", _clientId, username);
      createResponse(msg::server::AccessDenied);
      return false;
    }
  }

  // This response will be sent if there are any failures here.
  createResponse(msg::server::InternalError);

  HObject clientProcess;
  ULONG processId;
  if (!GetNamedPipeClientProcessId(_pipe, &processId)) {
    log::error("Client {}: Couldn't get client process ID: {}", _clientId,
               lastErrorString());
    return false;
  }
  auto const access =
    PROCESS_DUP_HANDLE | PROCESS_VM_READ | PROCESS_QUERY_INFORMATION;
  if (!(clientProcess = OpenProcess(access, false, processId))) {
    log::error("Client {}: Couldn't open client process: {}", _clientId,
               lastErrorString());
    return false;
  }

  HObject currentToken;
  // if (!OpenProcessToken(clientProcess, TOKEN_DUPLICATE | TOKEN_READ,
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_READ,
                        &currentToken))
  {
    log::error("Client {}: Couldn't open client process token: {}", _clientId,
               lastErrorString());
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
               lastErrorString());
    return false;
  }
  WSUDO_SCOPEEXIT { LocalFree(secDesc); };

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
               lastErrorString());
    return false;
  }

  _userToken = std::move(newToken);
  log::info("Client {}: Authorized; stored new token.", _clientId);

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

  if (!GetNamedPipeClientProcessId(_pipe, &processId)) {
    log::error("Client {}: Couldn't get client process ID: {}", _clientId,
               lastErrorString());
    return false;
  }
  auto const access = PROCESS_DUP_HANDLE | PROCESS_VM_READ;
  if (!(clientProcess = OpenProcess(access, false, processId))) {
    log::error("Client {}: Couldn't open client process: {}", _clientId,
               lastErrorString());
    return false;
  }
  log::debug("Trying to duplicate remote handle 0x{:X}.",
             reinterpret_cast<size_t>(remoteHandle));
  if (!DuplicateHandle(clientProcess, remoteHandle, GetCurrentProcess(),
                       &localHandle, PROCESS_SET_INFORMATION, false, 0))
  {
    log::error("Client {}: Couldn't duplicate remote handle: {}", _clientId,
               lastErrorString());
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
               lastErrorString());
    return false;
  }

  log::info("Client {}: Successfully adjusted remote process token.",
            _clientId);

  return true;
}
