#include "wsudo/wsudo.h"
#include "wsudo/config.h"
#include <AclAPI.h>
#include <ShlObj.h>
#include <Shlwapi.h>
#include <cstdlib>
#include <wil/resource.h>
#include <winnt.h>

using namespace wsudo;

void ServerConfig::setDefaults() noexcept {
  waitTimeout = 30;
  sessionTimeout = 5 * 60;
}

static bool setConfig(ServerConfig &config, const std::string &key,
                      std::string &value) noexcept {
  while (value.back() == ' ' || value.back() == '\t') {
    value.erase(value.end() - 1);
  }

  if (key == "wait-timeout") {
    char *end;
    unsigned long n = strtoul(value.c_str(), &end, 10);
    if (end != value.data() + value.length()) {
      log::error("Expected number for 'wait-timeout' in wsudo.conf");
      return false;
    }
    config.waitTimeout = (unsigned)n;
  } else if (key == "session-timeout") {
    char *end;
    unsigned long n = strtoul(value.c_str(), &end, 10);
    if (end != value.data() + value.length()) {
      log::error("Expected number for 'session-timeout' in wsudo.conf");
      return false;
    }
    config.sessionTimeout = (unsigned)n;
  } else {
    log::error("Unknown setting '{}' in wsudo.conf", key);
    return false;
  }

  return true;
}

bool ServerConfig::parse() noexcept {
  wchar_t *appDataPath;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr,
                                  &appDataPath))) {
    return false;
  }
  std::wstring configPath{appDataPath};
  CoTaskMemFree(appDataPath);
  configPath.append(L"\\wsudo\\wsudo.conf");

  wil::unique_hfile file{CreateFile(
    configPath.c_str(), GENERIC_READ | READ_CONTROL, FILE_SHARE_READ, nullptr,
    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr
  )};
  if (!file) {
    log::warn(L"Failed to open '{}'.", configPath);
    return false;
  }

  PSECURITY_DESCRIPTOR sd;
  PACL dacl;
  PSID owner;
  if (GetSecurityInfo(
    file.get(), SE_FILE_OBJECT,
    DACL_SECURITY_INFORMATION | OWNER_SECURITY_INFORMATION,
    &owner , nullptr, &dacl, nullptr , &sd
  ) != ERROR_SUCCESS) {
    log::error(L"Couldn't read security info on '{}'.", configPath);
    return false;
  }
  WSUDO_SCOPE_EXIT { LocalFree(sd); };

  constexpr DWORD SidSize = 256;
  DWORD cbSid = SidSize;
  PSID sidSystem{new char[SidSize]}, sidAdmin{new char[SidSize]};
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

  if (!EqualSid(owner, sidAdmin)) {
    log::error(L"'{}' is not owned by the Administrators group.", configPath);
    return false;
  }

  bool foundSystem = false, foundAdmin = false;
  for (DWORD i = 0; i < dacl->AceCount; ++i) {
    void *pvAce;
    if (!GetAce(dacl, i, &pvAce)) {
      return false;
    }

    auto *ace = static_cast<ACCESS_ALLOWED_ACE *>(pvAce);
    if (ace->Header.AceType != ACCESS_ALLOWED_ACE_TYPE) {
      log::error(L"Bad ACE type on '{}'.", configPath);
      return false;
    }
    auto *pSid = static_cast<PSID>(&ace->SidStart);
    if (EqualSid(pSid, sidSystem)) {
      foundSystem = true;
    } else if (EqualSid(pSid, sidAdmin)) {
      foundAdmin = true;
    } else {
      log::error(L"Only System and Administrators should have access to '{}'.",
                 configPath);
      return false;
    }
    if ((ace->Mask & (FILE_READ_DATA | FILE_WRITE_DATA))
        != (FILE_READ_DATA | FILE_WRITE_DATA)) {
      log::warn(L"No read/write access to '{}'.", configPath);
      return false;
    }
  }

  if (!foundSystem) {
    log::warn(L"System should have access to '{}'.", configPath);
  }
  if (!foundAdmin) {
    log::warn(L"Administrators should have access to '{}'.", configPath);
  }

  DWORD size = GetFileSize(file.get(), nullptr), bytesRead;
  std::string buffer;
  buffer.resize(size + 1);
  if (!ReadFile(file.get(), buffer.data(), size, &bytesRead, nullptr)
      || bytesRead < size) {
    log::error("Reading wsudo.conf failed: (0x{:X}): {}",
               GetLastError(), lastErrorString());
    return false;
  }
  buffer.push_back('\0');

  std::string key, value;
  enum {
    Initial,
    ReadingKey,
    ReadingWhitespace,
    ReadingValue,
    ReadingComment,
  } state = Initial;
  unsigned line = 1;
  for (DWORD i = 0; i < bytesRead; ++i) {
    char c = buffer[i];
    char l = c | 0x20;
    switch (state) {
    case Initial:
      if ((l >= 'a' && l <= 'z') || c == '-') {
        key.push_back(c);
        state = ReadingKey;
      } else if (c == '#') {
        state = ReadingComment;
      } else if (c == ' ' || c == '\t' || c == '\r' || c == '\0') {
        continue;
      } else if (c == '\n') {
        ++line;
        continue;
      } else {
        log::error("Unexpected character '{}' in wsudo.conf line {}", c, line);
        return false;
      }
      break;

    case ReadingKey:
      if ((l >= 'a' && l <= 'z') || c == '-') {
        key.push_back(c);
      } else if (c == ' ' || c == '\t') {
        state = ReadingWhitespace;
        continue;
      } else if (c == '\r' || c == '\n' || c == '\0') {
        log::error("Expected value in wsudo.conf line {}", line);
        return false;
      }
      break;

    case ReadingWhitespace:
      if (c == ' ' || c == '\t') {
        continue;
      } else if (c == '\r' || c == '\n' || c == '\0') {
        log::error("Expected value in wsudo.conf line {}", line);
        return false;
      } else {
        value.push_back(c);
        state = ReadingValue;
      }
      break;

    case ReadingValue:
      if (c == '\r') {
        if (!setConfig(*this, key, value)) {
          return false;
        }
        key.clear();
        value.clear();
        state = Initial;
      } else if (c == '\n') {
        if (!setConfig(*this, key, value)) {
          return false;
        }
        key.clear();
        value.clear();
        state = Initial;
        ++line;
      } else if (c == '\0') {
        if (!setConfig(*this, key, value)) {
          return false;
        }
      } else {
        value.push_back(c);
      }
      break;

    case ReadingComment:
      if (c == '\n') {
        ++line;
        state = Initial;
      }
      break;
    }
  }

  log::info(L"Processed '{}'.", configPath);

  return true;
}
