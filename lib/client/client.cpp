#include "wsudo/wsudo.h"
#include "wsudo/client.h"
#include "wsudo/message.h"
#include <Windows.h>
#include <variant>
#include <wil/result_macros.h>
#include <winerror.h>
#include <AclAPI.h>

#define SECURITY_WIN32
#include <Security.h>
#pragma comment(lib, "Secur32.lib")

#include <UserEnv.h>
#pragma comment(lib, "UserEnv.lib")

using namespace wsudo;

void Client::lookupUsername() {
  std::wstring domain;
  std::wstring username;
  ULONG usernameLength = 0;
  GetUserNameEx(NameSamCompatible, nullptr, &usernameLength);
  auto err = GetLastError();
  if (err == ERROR_MORE_DATA) {
    username.resize(usernameLength);
    if (!GetUserNameEx(NameSamCompatible, username.data(), &usernameLength)) {
      log::critical("Can't get username.\n");
      THROW_LAST_ERROR();
    }
    // It ends in a null that we don't want copied.
    username.erase(username.cend() - 1);
  } else {
    log::critical("Can't get username.\n");
    THROW_WIN32(err);
  }

  auto slash = username.find(L'\\');
  domain = username.substr(0, slash);
  _domain = to_utf8(domain);
  username.erase(0, slash + 1);
  _username = to_utf8(username);

  wchar_t computerName[MAX_COMPUTERNAME_LENGTH + 1];
  DWORD computerNameLength = MAX_COMPUTERNAME_LENGTH;
  if (GetComputerName(computerName, &computerNameLength)) {
    if (CompareString(LOCALE_INVARIANT, NORM_IGNORECASE,
                      computerName, -1, domain.c_str(), -1) == CSTR_EQUAL) {
      // Ignore local domain.
      _domain = ".";
    }
  }

  log::debug("Current user: {}\\{}", _domain, _username);
}

std::wstring Client::getUserDirectory() const {
  wil::unique_handle processToken;
  THROW_IF_WIN32_BOOL_FALSE(
    OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, processToken.put())
  );
  DWORD size;
  GetUserProfileDirectory(processToken.get(), nullptr, &size);
  std::wstring profileDir;
  profileDir.resize(size);
  THROW_IF_WIN32_BOOL_FALSE(
    GetUserProfileDirectory(processToken.get(), profileDir.data(), &size)
  );
  profileDir.erase(profileDir.cend() - 1); // NUL
  return profileDir;
}

std::wstring Client::getUserDirectoryFileName(std::wstring_view filename)
    const {
  auto path = getUserDirectory();
  if (!path.ends_with(L'\\')) {
    path.push_back(L'\\');
  }
  path.append(filename);
  return path;
}

bool Client::checkPrivateFliePermissions(HANDLE file,
                                         std::wstring_view filename) const {
  log::debug(L"Checking permissions on \"{}\".", filename);
  PSECURITY_DESCRIPTOR secDesc;
  PACL dacl;
  PSID owner;
  if (GetSecurityInfo(
    file, SE_FILE_OBJECT,
    DACL_SECURITY_INFORMATION | OWNER_SECURITY_INFORMATION,
    &owner, nullptr, &dacl, nullptr, &secDesc
  ) != ERROR_SUCCESS) {
    return false;
  }
  WSUDO_SCOPE_EXIT { LocalFree(secDesc); };

  constexpr DWORD SidSize = 256;
  DWORD cbSid = SidSize;
  PSID sidSystem{new char[SidSize]}, sidAdmin{new char[SidSize]}, sidUser;
  WSUDO_SCOPE_EXIT {
    delete[] reinterpret_cast<char *>(sidSystem);
    delete[] reinterpret_cast<char *>(sidAdmin);
  };

  if (!CreateWellKnownSid(WinLocalSystemSid, nullptr, sidSystem, &cbSid)) {
    return false;
  }

  cbSid = SidSize;
  if (!CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, sidAdmin,
                          &cbSid)) {
    return false;
  }

  wil::unique_handle token;
  union {
    TOKEN_USER tokenUser;
    char cdata[SidSize];
  };
  DWORD length;
  if (!OpenProcessToken(GetCurrentProcess(), READ_CONTROL | TOKEN_READ,
                        &token)) {
    log::error("Couldn't open current process token.");
    return false;
  }
  if (!GetTokenInformation(
    token.get(), TokenUser, reinterpret_cast<LPVOID>(&tokenUser),
    SidSize, &length
  )) {
    log::debug("Couldn't get process token information: (0x{:X}): {}.",
               GetLastError(), lastErrorString());
    return false;
  }
  sidUser = tokenUser.User.Sid;

  if (!EqualSid(owner, sidUser)) {
    log::error(L"File \"{}\" is owned by another user.", filename);
    return false;
  }

  bool foundSystem = false, foundAdmin = false, foundUser = false;
  for (DWORD i = 0; i < dacl->AceCount; ++i) {
    void *pvAce;
    if (!GetAce(dacl, i, &pvAce)) {
      return false;
    }
    auto *ace = static_cast<ACCESS_ALLOWED_ACE *>(pvAce);
    if (ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE) {
      if ((ace->Mask & (FILE_READ_DATA | FILE_WRITE_DATA)) !=
          (FILE_READ_DATA | FILE_WRITE_DATA)) {
        return false;
      }
      auto *pSid = static_cast<PSID>(&ace->SidStart);
      if (EqualSid(pSid, sidSystem)) {
        foundSystem = true;
        log::debug(L"Found system SID.");
      } else if (EqualSid(pSid, sidAdmin)) {
        foundAdmin = true;
        log::debug(L"Found admin SID.");
      } else if (EqualSid(pSid, sidUser)) {
        foundUser = true;
        log::debug(L"Found user SID.");
      } else {
        log::error(L"Unexpected security entry for file \"{}\".", filename);
        return false;
      }
    } else {
      return false;
    }
  }

  return foundSystem && foundAdmin && foundUser;
}

std::string
Client::readPrivateFile(std::wstring_view name, bool create) const {
  auto path = getUserDirectoryFileName(name);
  log::trace(L"Reading private file \"{}\".", path);

  DWORD openMode = create ? OPEN_ALWAYS : OPEN_EXISTING;
  SECURITY_ATTRIBUTES sec;
  sec.nLength = sizeof(SECURITY_ATTRIBUTES);
  sec.lpSecurityDescriptor = nullptr;
  sec.bInheritHandle = false;
  wil::unique_hfile file{
    CreateFile(path.c_str(), GENERIC_READ | READ_CONTROL, 0, &sec, openMode,
               FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr)
  };
  THROW_LAST_ERROR_IF(file.get() == INVALID_HANDLE_VALUE);

  if (!checkPrivateFliePermissions(file.get(), name)) {
    auto err = GetLastError();
    if (!DeleteFile(std::wstring{name}.c_str())) {
      err = GetLastError();
    }
    THROW_WIN32(err);
  }

  std::string text;
  DWORD fileSize = GetFileSize(file.get(), nullptr);
  log::trace("Reading {} bytes.", fileSize);
  THROW_LAST_ERROR_IF(fileSize == INVALID_FILE_SIZE);
  text.resize(fileSize);

  DWORD bytesRead, totalBytesRead = 0;
  while (totalBytesRead < fileSize) {
    THROW_IF_WIN32_BOOL_FALSE(
      ReadFile(file.get(), text.data() + totalBytesRead,
               fileSize - totalBytesRead, &bytesRead, nullptr)
    );
    totalBytesRead += bytesRead;
  }

  return text;
}

void Client::writePrivateFile(std::wstring_view name, std::string_view text)
    const {
  auto path = getUserDirectoryFileName(name);
  log::trace(L"Writing private file \"{}\".", path);

  wil::unique_hfile file{
    CreateFile(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
               FILE_ATTRIBUTE_NORMAL, nullptr)
  };
  THROW_LAST_ERROR_IF(file.get() == INVALID_HANDLE_VALUE);

  DWORD bytesWritten, totalBytesWritten = 0;
  while (totalBytesWritten < text.length()) {
    THROW_IF_WIN32_BOOL_FALSE(
      WriteFile(file.get(), text.data() + totalBytesWritten,
                text.length() - totalBytesWritten, &bytesWritten, nullptr)
    );
    totalBytesWritten += bytesWritten;
  }
}

bool Client::readConsolePassword(std::string &password) const {
  if (_domain == ".") {
    log::print("[wsudo] password for {}: ", _username);
  } else {
    log::print("[wsudo] password for {}\\{}: ", _domain, _username);
  }

  fflush(stdout);

  HANDLE hStdIn = GetStdHandle(STD_INPUT_HANDLE);
  DWORD previousInMode;
  GetConsoleMode(hStdIn, &previousInMode);
  SetConsoleMode(hStdIn, ENABLE_EXTENDED_FLAGS | ENABLE_QUICK_EDIT_MODE);
  WSUDO_SCOPE_EXIT { SetConsoleMode(hStdIn, previousInMode); };

  std::wstring wpassword;
  WSUDO_SCOPE_EXIT { wpassword.assign(wpassword.length(), L'\0'); };
  while (true) {
    wchar_t ch;
    DWORD chRead;
    if (!ReadConsole(hStdIn, &ch, 1, &chRead, nullptr)) {
      return false;
    }
    if (ch == 13 || ch == 10) {
      // Enter
      log::print("\n");
      break;
    } else if (ch == 8 || ch == 0x7F) {
      // Backspace
      if (wpassword.length() > 1) {
        auto lo = *(wpassword.cend() - 1);
        auto hi = *(wpassword.cend() - 2);
        if (lo >= 0xD800 && lo <= 0xDBFF && hi >= 0xDC00 && hi <= 0xDFFF) {
          // Surrogate pair.
          wpassword.erase(wpassword.cend() - 2);
        } else {
          wpassword.erase(wpassword.cend() - 1);
        }
      } else if (wpassword.length() > 0) {
        wpassword.erase(wpassword.cend() - 1);
      }
    } else if (ch == 3) {
      // Ctrl-C
      log::print("\nCanceled.\n");
      return false;
    } else {
      wpassword.push_back(ch);
    }
  }
  // TODO: make sure this doesn't leave the password in memory somewhere.
  // better to convert 1 char at a time while reading.
  password = to_utf8(wpassword);
  return true;
}

PROCESS_INFORMATION
Client::createSuspendedProcess(std::wstring &commandLine) {
  STARTUPINFOW si{};
  si.cb = sizeof(STARTUPINFOW);
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
  si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  si.dwFlags = STARTF_USESTDHANDLES;
  PROCESS_INFORMATION pi{};
  // The system is allowed to modify this string, so we can't use c_str as it
  // returns a const pointer.
  commandLine.push_back(L'\0');
  if (!CreateProcess(nullptr, commandLine.data(), nullptr, nullptr, true,
                     CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED,
                     nullptr, nullptr, &si, &pi)) {
    log::error("Process creation failed.");
    THROW_LAST_ERROR();
  }
  return pi;
}

bool Client::userHasActiveSession() {
  if (_key.empty()) {
    return false;
  }
  _conn.send(msg::QuerySession{_domain, _username, _key});
  return std::holds_alternative<msg::Success>(_conn.recv());
}

bool Client::validateCredentials(std::string &password) {
  Message res;
  try {
    _conn.send(msg::Credential{_domain, _username, password});
    res = _conn.recv();
  } catch (wil::ResultException &e) {
    log::error("Validate credentials failed: (0x{:X}): {}",
               static_cast<unsigned>(e.GetErrorCode()),
               lastErrorString(e.GetErrorCode()));
    return false;
  }
  if (std::holds_alternative<msg::Success>(res)) {
    log::warn("Credential validation returned success but a session key is needed.");
    return true;
  } else if (std::holds_alternative<msg::InitSession>(res)) {
    try {
      log::trace("Initializing new session.");
      _key = std::get<msg::InitSession>(res).key;
      writePrivateFile(UserSessionKeyFileName, _key);
      return true;
    } catch (wil::ResultException &e) {
      log::warn("Failed to initialize session: (0x{:X}): {}",
                static_cast<unsigned>(e.GetErrorCode()),
                lastErrorString(e.GetErrorCode()));
      return false;
    }
  } else if (std::holds_alternative<msg::Failure>(res)) {
    log::error("Invalid credentials.");
  } else if (std::holds_alternative<msg::Invalid>(res)) {
    auto &inv = std::get<msg::Invalid>(res);
    log::error(
      "Received invalid message: {}: {}", inv.originalCode, inv.payload
    );
  } else {
    log::error(
      "Unknown message while validating credentials: {}.", res.getCode()
    );
  }
  return false;
}

bool Client::bless(HANDLE process, bool runAsSystem) {
  Message res;

  if (_key.empty()) {
    log::error("Missing session key.");
    return false;
  }

  try {
    _conn.send(msg::Bless{_key, process, runAsSystem});
    res = _conn.recv();
  } catch (wil::ResultException &e) {
    log::error(
      "Could not write to pipe: (0x{:X}): {}",
      static_cast<unsigned>(e.GetErrorCode()),
      lastErrorString(e.GetErrorCode())
    );
    return false;
  }

  if (std::holds_alternative<msg::Success>(res)) {
    log::trace("Bless process succeeded.");
    return true;
  } else if (std::holds_alternative<msg::Failure>(res)) {
    log::error("Bless process failed.");
  } else if (std::holds_alternative<msg::AccessDenied>(res)) {
    log::error("Bless: access denied.");
  }
  return false;
}

int Client::resume(PROCESS_INFORMATION &pi, bool wait) {
  DWORD exitCode = 0;
  // TODO: Return errors if any of this fails.
  ResumeThread(pi.hThread);
  if (wait) {
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &exitCode);
  }
  return static_cast<int>(exitCode);
}

Client::Client(const std::wstring &pipeName)
  : _conn{pipeName}
{
  lookupUsername();

  try {
    _key = readPrivateFile(UserSessionKeyFileName);
  } catch (wil::ResultException &e) {
    if (e.GetErrorCode() == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) {
      log::debug("Session key not found.");
    } else if (e.GetErrorCode() == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED)) {
      log::warn("Access denied to session key file.");
    } else {
      log::warn("Exception while reading session key: (0x{:X}): {}",
                static_cast<unsigned>(e.GetErrorCode()),
                lastErrorString(e.GetErrorCode()));
    }
  }
}

int Client::operator()(Options &options) {
  if (options.commandLine.empty()) {
    return ERROR_FILE_NOT_FOUND;
  }

  if (!userHasActiveSession()) {
    std::string password;
    WSUDO_SCOPE_EXIT { password.assign(password.length(), '\0'); };
    if (!readConsolePassword(password)) {
      return ERROR_CANCELLED;
    }
    if (!validateCredentials(password)) {
      return ERROR_LOGON_FAILURE;
    }
  }

  PROCESS_INFORMATION pi{};
  try {
    pi = createSuspendedProcess(options.commandLine);
  } catch (wil::ResultException &) {
    return ERROR_CHILD_PROCESS_BLOCKED;
  }
  WSUDO_SCOPE_EXIT {
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
  };

  if (!bless(pi.hProcess, options.runAsSystem)) {
    TerminateProcess(pi.hProcess, (UINT)(-1));
    return ERROR_ACCESS_DENIED;
  }

  _conn.disconnect();

  log::debug("Resuming process {}.", pi.hProcess);
  return resume(pi, !options.detach);
}
