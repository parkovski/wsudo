#include "stdo/server.h"
#include "stdo/ntapi.h"

#include <aclapi.h>

#pragma comment(lib, "Advapi32.lib")

using namespace stdo;
using namespace stdo::server;

EventStatus EventHandlerOverlapped::beginRead(HANDLE hFile) {
  _overlapped->Offset = 0;
  _overlapped->OffsetHigh = 0;
  _buffer.resize(PipeBufferSize);
  if (ReadFile(hFile, _buffer.data(), PipeBufferSize, nullptr,
               _overlapped.get()))
  {
    return EventStatus::Finished;
  } else if (GetLastError() == ERROR_IO_PENDING) {
    return EventStatus::InProgress;
  } else {
    log::error("ReadFile failed: {}", getSystemStatusString(GetLastError()));
    return EventStatus::Failed;
  }
}

EventStatus EventHandlerOverlapped::beginWrite(HANDLE hFile) {
  _overlapped->Offset = 0;
  _overlapped->OffsetHigh = 0;
  if (WriteFile(hFile, _buffer.data(), (DWORD)_buffer.size(), nullptr,
                _overlapped.get()))
  {
    return EventStatus::Finished;
  } else if (GetLastError() == ERROR_IO_PENDING) {
    return EventStatus::InProgress;
  } else {
    log::error("WriteFile failed: {}", getSystemStatusString(GetLastError()));
    return EventStatus::Failed;
  }
}

EventStatus EventHandlerOverlapped::endReadWrite(HANDLE hFile) {
  DWORD bytesTransferred;
  DWORD error = getOverlappedResult(hFile, &bytesTransferred);
  if (error == ERROR_SUCCESS) {
    _buffer.resize(bytesTransferred);
    return EventStatus::Finished;
  } else if (error == ERROR_IO_INCOMPLETE) {
    return EventStatus::InProgress;
  }
  log::error("GetOverlappedResult failed: {}", getSystemStatusString(error));
  return EventStatus::Failed;
}

DWORD EventHandlerOverlapped::getOverlappedResult(HANDLE hFile, DWORD *bytes) {
  DWORD bytesTransferred;
  DWORD error;
  if (
    GetOverlappedResult(hFile, _overlapped.get(), &bytesTransferred, false) ||
    (error = GetLastError()) == ERROR_HANDLE_EOF
  )
  {
    error = ERROR_SUCCESS;
    ResetEvent(_overlapped->hEvent);
  } else {
    bytesTransferred = 0;
  }
  if (bytes) {
    *bytes = bytesTransferred;
  }
  return error;
}

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
    break;
  case ERROR_PIPE_CONNECTED:
    log::trace("Client {}: already connected; setting read event.", _clientId);
    SetEvent(_overlapped->hEvent);
    break;
  default:
    // We don't have an active connection, so keep the callback and pipe handle
    // null and set the event that will return failed and remove this object
    // from the event queue.
    log::error("Client {}: ConnectNamedPipe failed: {}", _clientId,
               getSystemStatusString(GetLastError()));
    SetEvent(_overlapped->hEvent);
    return nullptr;
  }

  _connection = pipe;
  return &ClientConnectionHandler::finishConnect;
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
      createResponse(SMsgHeaderInternalError, "Unknown error.");
    }
  };

  if (!std::memcmp(header, MsgHeaderCredential, 4)) {
    // Verify the username/password pair.
    auto bufferEnd = _buffer.cend();
    auto usernameBegin = _buffer.cbegin() + 4;
    auto usernameEnd = usernameBegin;
    while (true) {
      if (usernameEnd == bufferEnd) {
        log::warn("Client {}: Password not found in message.", _clientId);
        createResponse(SMsgHeaderInvalidMessage,
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
        createResponse(SMsgHeaderInvalidMessage,
                       "Incorrect password format.");
        return false;
      }
      ++passwordEnd;
    }
    _buffer.emplace_back(0);
    return tryToLogonUser(&*usernameBegin, &*passwordBegin);
  } else if (!std::memcmp(header, MsgHeaderBless, 4)) {
    if (_buffer.size() != 4 + sizeof(HANDLE)) {
      log::warn("Client {}: Invalid bless message.", _clientId);
      createResponse(SMsgHeaderInvalidMessage);
    } else {
      HANDLE remoteHandle;
      std::memcpy(&remoteHandle, _buffer.data() + 4, sizeof(HANDLE));
      if (bless(remoteHandle)) {
        createResponse(SMsgHeaderSuccess);
      } else {
        createResponse(SMsgHeaderInternalError,
                       "Token substitution failed.");
      }
    }
    return false;
  } else {
    log::warn("Client {}: Unknown message header {}.", _clientId, header);
    createResponse(SMsgHeaderInvalidMessage, "Unknown message header");
    return false;
  }
}

bool ClientConnectionHandler::tryToLogonUser(const char *username,
                                             const char *password)
{
  // FIXME: Yeah...
  if (std::memcmp(password, "password", 9)) {
    log::trace("Client {}: Access denied - invalid password.", _clientId);
    createResponse(SMsgHeaderAccessDenied);
    return false;
  }

  createResponse(SMsgHeaderInternalError);

  HObject clientProcess;
  ULONG processId;
  if (!GetNamedPipeClientProcessId(_connection, &processId)) {
    log::error("Client {}: Couldn't get client process ID: {}", _clientId,
               getSystemStatusString(GetLastError()));
    return false;
  }
  auto const access =
    PROCESS_DUP_HANDLE | PROCESS_VM_READ | PROCESS_QUERY_INFORMATION;
  if (!(clientProcess = OpenProcess(access, false, processId))) {
    log::error("Client {}: Couldn't open client process: {}", _clientId,
               getSystemStatusString(GetLastError()));
    return false;
  }

  HObject currentToken;
  // if (!OpenProcessToken(clientProcess, TOKEN_DUPLICATE | TOKEN_READ,
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_READ,
                        &currentToken))
  {
    log::error("Client {}: Couldn't open client process token: {}", _clientId,
               getSystemStatusString(GetLastError()));
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
               getSystemStatusString(GetLastError()));
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
               getSystemStatusString(GetLastError()));
    return false;
  }

  _userToken = std::move(newToken);
  log::trace("Client {}: Stored new token.", _clientId);

  createResponse(SMsgHeaderSuccess);
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
               getSystemStatusString(GetLastError()));
    return false;
  }
  auto const access = PROCESS_DUP_HANDLE | PROCESS_VM_READ;
  if (!(clientProcess = OpenProcess(access, false, processId))) {
    log::error("Client {}: Couldn't open client process: {}", _clientId,
               getSystemStatusString(GetLastError()));
    return false;
  }
  log::trace("Trying to duplicate handle 0x{:X}", reinterpret_cast<size_t>(remoteHandle));
  if (!DuplicateHandle(clientProcess, remoteHandle, GetCurrentProcess(),
                       &localHandle, PROCESS_SET_INFORMATION, false, 0))
  {
    log::error("Client {}: Couldn't duplicate remote handle: {}", _clientId,
               getSystemStatusString(GetLastError()));
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
               getSystemStatusString(GetLastError()));
    return false;
  }

  log::trace("Client {}: Adjusted remote process token: {}", _clientId,
             getSystemStatusString(GetLastError()));

  return true;
}

std::unique_ptr<EventHandler> EventListener::remove(size_t index) {
  auto elem = std::move(_handlers[index]);
  _events.erase(_events.cbegin() + index);
  _handlers.erase(_handlers.cbegin() + index);
  return elem;
}

Status EventListener::eventLoop(DWORD timeout) {
  while (true) {
    log::trace("Waiting on {} events", _events.size());
    auto result = WaitForMultipleObjects(
      (DWORD)_events.size(), &_events[0], false, timeout
    );

    if (result == WAIT_TIMEOUT) {
      log::warn("WaitForMultipleObjects timed out.");
      return StatusTimedOut;
    } else if (result >= WAIT_OBJECT_0 &&
               result < WAIT_OBJECT_0 + _events.size())
    {
      size_t index = (size_t)(result - WAIT_OBJECT_0);
      log::trace("Event #{} signaled.", index);
      if (index == ExitLoopIndex) {
        return StatusOk;
      }
      switch ((*_handlers[index])()) {
      case EventStatus::InProgress:
        log::trace("Event #{} returned InProgress.", index);
        ResetEvent(_events[index]);
        break;
      case EventStatus::Finished:
        log::trace("Event #{} returned Finished.", index);
        remove(index);
        break;
      case EventStatus::Failed:
        log::error("Event #{} returned Failed.", index);
        return StatusEventFailed;
        break;
      }
    } else if (result >= WAIT_ABANDONED_0 &&
               result < WAIT_ABANDONED_0 + _events.size())
      
    {
      size_t index = (size_t)(result - WAIT_ABANDONED_0);
      log::error("Mutex abandoned state signaled for handler #{}.", index);
      throw event_mutex_abandoned_error{remove(index)};
    } else if (result == WAIT_FAILED) {
      auto error = GetLastError();
      log::error("WaitForMultipleObjects failed: {}",
                 getSystemStatusString(error));
      throw event_wait_failed_error{error};
    } else {
      log::critical("WaitForMultipleObjects returned 0x{:X}.", result);
      std::terminate();
    }
  }
}

template<typename F>
static
std::enable_if_t<std::is_invocable_r_v<bool, F, LPSECURITY_ATTRIBUTES>, bool>
doWithSecurityAttributes(F fn) {
  Handle<PSID, FreeSid> sidEveryone{};
  SID_IDENTIFIER_AUTHORITY sidAuthWorld = SECURITY_WORLD_SID_AUTHORITY;
  if (!AllocateAndInitializeSid(&sidAuthWorld, 1, SECURITY_WORLD_RID, 0, 0, 0,
                                0, 0, 0, 0, &sidEveryone))
  {
    return false;
  }

  EXPLICIT_ACCESS_W ea;
  ea.grfAccessPermissions = SYNCHRONIZE | GENERIC_READ | GENERIC_WRITE;
  ea.grfAccessMode = SET_ACCESS;
  ea.grfInheritance = NO_INHERITANCE;
  ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
  ea.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
  ea.Trustee.ptstrName = (LPWSTR)&sidEveryone;

  Handle<PACL, LocalFree> acl{};
  if (!SUCCEEDED(SetEntriesInAclW(1, &ea, nullptr, &acl))) {
    return false;
  }

  Handle<PSECURITY_DESCRIPTOR, LocalFree> secDesc{
    LocalAlloc(LPTR, SECURITY_DESCRIPTOR_MIN_LENGTH)
  };
  if (!secDesc) {
    return false;
  }

  if (!InitializeSecurityDescriptor(secDesc, SECURITY_DESCRIPTOR_REVISION)) {
    return false;
  }

  if (!SetSecurityDescriptorDacl(secDesc, true, acl, false)) {
    return false;
  }

  SECURITY_ATTRIBUTES secAttr;
  secAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
  secAttr.bInheritHandle = false;
  secAttr.lpSecurityDescriptor = secDesc;

  return fn(&secAttr);
}

#define FINISH(exitCode) do { config.status = exitCode; return; } while (false)
void stdo::server::serverMain(Config &config) {
  HObject pipe{};
  if (!doWithSecurityAttributes([&](LPSECURITY_ATTRIBUTES sa) -> bool {
		pipe = CreateNamedPipeW(config.pipeName.c_str(),
														PIPE_ACCESS_DUPLEX |
															FILE_FLAG_FIRST_PIPE_INSTANCE |
															FILE_FLAG_OVERLAPPED,
														PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE |
															PIPE_REJECT_REMOTE_CLIENTS,
														MaxPipeConnections, PipeBufferSize, PipeBufferSize,
														PipeDefaultTimeout, sa);
		return !!pipe;
	}))
  {
    FINISH(StatusCreatePipeFailed);
  }

  EventListener listener{QuitHandler{config.quitEvent}};
  listener.push(ClientConnectionHandler{pipe, 1});
  FINISH(listener.eventLoop());
}

