#include "wsudo/wsudo.h"
#include "wsudo/server.h"
#include <Lm.h>
#include <accctrl.h>
#include <fileapi.h>
#include <minwinbase.h>
#include <securitybaseapi.h>
#include <winnt.h>
#define SECURITY_WIN32
#include <Security.h>
#include <ShlObj.h>
#include <Shlwapi.h>
#include <wil/result_macros.h>

#pragma comment(lib, "Netapi32.lib")
#pragma comment(lib, "Secur32.lib")

HRESULT wsudo::installService() {
  {
    HKEY hKey;
    LSTATUS status;
    status = RegCreateKeyExW(
      HKEY_LOCAL_MACHINE,
      L"SYSTEM\\CurrentControlSet\\Services\\EventLog\\wsudo\\wsudo", 0,
      nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr
    );
    if (status != ERROR_SUCCESS) {
      log::error("Could not open registry.");
      RETURN_WIN32(status);
    }
    WSUDO_SCOPE_EXIT { RegCloseKey(hKey); };

    const DWORD TypesSupported = 7;
    status = RegSetValueExW(
      hKey, L"TypesSupported", 0, REG_DWORD,
      reinterpret_cast<const BYTE *>(&TypesSupported),
      sizeof(DWORD)
    );
    if (status != ERROR_SUCCESS) {
      log::error("Could not write to registry.");
      RETURN_WIN32(status);
    }

    const wchar_t *const EventMessageFile =
      L"%SystemRoot%\\System32\\mscoree.dll";
    status = RegSetValueExW(
      hKey, L"EventMessageFile", 0, REG_SZ,
      reinterpret_cast<const BYTE *>(EventMessageFile),
      (wcslen(EventMessageFile) + 1) * sizeof(wchar_t)
    );
    if (status != ERROR_SUCCESS) {
      log::error("Could not write to registry.");
      RETURN_WIN32(status);
    }
  }

  {
    DWORD pathSize = MAX_PATH, copied;
    wchar_t *path = new wchar_t[pathSize];
    do {
      copied = GetModuleFileName(nullptr, path, pathSize);
      if (copied == pathSize && GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
        delete[] path;
        pathSize *= 2;
        path = new wchar_t[pathSize];
      } else if (copied == 0) {
        RETURN_WIN32(GetLastError());
      } else {
        break;
      }
    } while (true);
    auto escapedPath = escapeCommandLineArg(std::wstring_view{path, copied});
    delete[] path;

    auto scManager = OpenSCManagerW(nullptr, nullptr,
                                    SC_MANAGER_CREATE_SERVICE);
    if (!scManager) {
      log::error("Could not open service manager.");
      RETURN_WIN32(GetLastError());
    }
    WSUDO_SCOPE_EXIT { CloseServiceHandle(scManager); };

    auto service = CreateServiceW(
      scManager, L"wsudo", L"WSudo Token Service",
      0, SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START,
      SERVICE_ERROR_NORMAL, escapedPath.c_str(),
      nullptr, nullptr, nullptr, nullptr, nullptr
    );
    if (!service) {
      log::error("Could not create service.");
      RETURN_WIN32(GetLastError());
    }
    WSUDO_SCOPE_EXIT { CloseServiceHandle(service); };
    log::info("Created service wsudo.");
  }

  {
    LOCALGROUP_INFO_1 lgrpInfo;
    lgrpInfo.lgrpi1_name = const_cast<wchar_t *>(L"wsudoers");
    lgrpInfo.lgrpi1_comment = const_cast<wchar_t *>(
      L"Users allowed to use wsudo to elevate"
    );
    auto grpResult = NetLocalGroupAdd(
      nullptr, 1, reinterpret_cast<BYTE *>(&lgrpInfo), nullptr
    );
    if (grpResult != NERR_Success) {
      log::error("Could not create local group wsudoers.");
      RETURN_WIN32(grpResult);
    }
    log::info("Created group wsudoers.");
  }

  {
    PSECURITY_DESCRIPTOR secDesc;
    PACL dacl;
    wchar_t serviceName[] = L"wsudo";
    DWORD result = GetNamedSecurityInfo(
      serviceName, SE_SERVICE, DACL_SECURITY_INFORMATION,
      nullptr, nullptr, &dacl, nullptr, &secDesc
    );
    if (result != ERROR_SUCCESS) {
      log::error("Failed to read service security information.");
      RETURN_WIN32(result);
    }
    WSUDO_SCOPE_EXIT { LocalFree(secDesc); };

    EXPLICIT_ACCESS ea{};
    ea.grfAccessPermissions =
      SERVICE_START | SERVICE_QUERY_STATUS | SERVICE_QUERY_CONFIG;
    ea.grfAccessMode = GRANT_ACCESS;
    ea.grfInheritance = NO_INHERITANCE;
    ea.Trustee.pMultipleTrustee = nullptr;
    ea.Trustee.MultipleTrusteeOperation = NO_MULTIPLE_TRUSTEE;
    ea.Trustee.TrusteeForm = TRUSTEE_IS_NAME;
    ea.Trustee.TrusteeType = TRUSTEE_IS_GROUP;
    wchar_t groupName[] = L"wsudoers";
    ea.Trustee.ptstrName = groupName;

    PACL newDacl;
    result = SetEntriesInAcl(1, &ea, dacl, &newDacl);
    if (result != ERROR_SUCCESS) {
      log::error("Failed to add wsudoers group to service ACL.");
      RETURN_WIN32(result);
    }
    WSUDO_SCOPE_EXIT { LocalFree(newDacl); };

    result = SetNamedSecurityInfo(
      serviceName, SE_SERVICE, DACL_SECURITY_INFORMATION,
      nullptr, nullptr, newDacl, nullptr
    );
    if (result != ERROR_SUCCESS) {
      log::error("Failed to write service security information.");
      RETURN_WIN32(result);
    }

    log::info("Granted wsudoers access to start the wsudo service.");

    std::wstring username;
    ULONG usernameLength = 0;
    GetUserNameEx(NameSamCompatible, nullptr, &usernameLength);
    if (GetLastError() == ERROR_MORE_DATA) {
      username.resize(usernameLength);
      if (GetUserNameEx(NameSamCompatible, username.data(), &usernameLength)) {
        auto slashPos = username.find(L'\\');
        if (slashPos != std::wstring::npos) {
          username.erase(0, slashPos + 1);
        }
        log::info(L"You can add yourself to the wsudoers group by typing "
                  L"\"net localgroup wsudoers {} /add\". "
                  L"Note that you will need to log out and in again "
                  L"for this to take effect.", username);
      }
    }
  }

  {
    SECURITY_ATTRIBUTES sa;
    PSECURITY_DESCRIPTOR psd;
    PACL dacl;
    PSID sidSystem, sidAdmin, sidAuthUsers;
    EXPLICIT_ACCESS ea[3];
    constexpr DWORD SidSize = 256;
    DWORD cbSid;
    sidSystem = new char[SidSize];
    sidAdmin = new char[SidSize];
    sidAuthUsers = new char[SidSize];
    WSUDO_SCOPE_EXIT {
      delete[] reinterpret_cast<char *>(sidSystem);
      delete[] reinterpret_cast<char *>(sidAdmin);
      delete[] reinterpret_cast<char *>(sidAuthUsers);
    };

    cbSid = SidSize;
    if (!CreateWellKnownSid(WinLocalSystemSid, nullptr, sidSystem, &cbSid)) {
      log::error("Failed to look up System account SID.");
      RETURN_LAST_ERROR();
    }

    cbSid = SidSize;
    if (!CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, sidAdmin,
                            &cbSid)) {
      log::error("Failed to look up Administrators SID.");
      RETURN_LAST_ERROR();
    }

    cbSid = SidSize;
    if (!CreateWellKnownSid(WinAuthenticatedUserSid, nullptr, sidAuthUsers,
                            &cbSid)) {
      log::error("Failed to look up authenticated users SID.");
      RETURN_LAST_ERROR();
    }

    for (int i = 0; i < 2; ++i) {
      ea[i].grfAccessPermissions = FILE_ALL_ACCESS;
      ea[i].grfAccessMode = SET_ACCESS;
      ea[i].grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
      ea[i].Trustee.pMultipleTrustee = nullptr;
      ea[i].Trustee.MultipleTrusteeOperation = NO_MULTIPLE_TRUSTEE;
      ea[i].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    }
    ea[0].Trustee.TrusteeType = TRUSTEE_IS_USER;
    ea[0].Trustee.ptstrName = (LPWCH)sidSystem;
    ea[1].Trustee.TrusteeType = TRUSTEE_IS_GROUP;
    ea[1].Trustee.ptstrName = (LPWCH)sidAdmin;

    ea[2].grfAccessPermissions =
      FILE_TRAVERSE | FILE_READ_ATTRIBUTES | FILE_READ_EA
      | STANDARD_RIGHTS_READ | FILE_EXECUTE | FILE_LIST_DIRECTORY;
    ea[2].grfAccessMode = SET_ACCESS;
    ea[2].grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
    ea[2].Trustee.pMultipleTrustee = nullptr;
    ea[2].Trustee.MultipleTrusteeOperation = NO_MULTIPLE_TRUSTEE;
    ea[2].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea[2].Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    ea[2].Trustee.ptstrName = (LPWCH)sidAuthUsers;

    auto result = SetEntriesInAcl(WSUDO_ARRAYSIZE(ea), ea, nullptr, &dacl);
    if (result != ERROR_SUCCESS) {
      log::error("Failed to set ACL entries");
      RETURN_WIN32(result);
    }
    WSUDO_SCOPE_EXIT { LocalFree(dacl); };

    psd = LocalAlloc(LPTR, SECURITY_DESCRIPTOR_MIN_LENGTH);
    WSUDO_SCOPE_EXIT { LocalFree(psd); };
    if (!InitializeSecurityDescriptor(psd, SECURITY_DESCRIPTOR_REVISION)) {
      log::error("Failed to initialize security descriptor.");
      RETURN_LAST_ERROR();
    }

    if (!SetSecurityDescriptorOwner(psd, sidAdmin, false)) {
      log::error("Failed to set security descriptor owner.");
      RETURN_LAST_ERROR();
    }

    if (!SetSecurityDescriptorDacl(psd, true, dacl, false)) {
      log::error("Failed to set security descriptor DACL.");
      RETURN_LAST_ERROR();
    }

    if (!SetSecurityDescriptorControl(psd, SE_DACL_PROTECTED,
                                      SE_DACL_PROTECTED)) {
      log::error("Failed to mark DACL as protected.");
      RETURN_LAST_ERROR();
    }

    sa.lpSecurityDescriptor = psd;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = false;

    wchar_t *appDataPath;
    auto hr = SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr,
                                   &appDataPath);
    if (FAILED(hr)) {
      log::error("Couldn't get ProgramData path.");
      return hr;
    }
    std::wstring wsudoPath{appDataPath};
    CoTaskMemFree(appDataPath);
    wsudoPath.append(L"\\wsudo");
    if (!CreateDirectory(wsudoPath.c_str(), &sa)) {
      log::error(L"Failed to create directory '{}'.", wsudoPath);
      RETURN_LAST_ERROR();
    }

    wsudoPath.append(L"\\wsudo.conf");
    auto hFile = CreateFile(
      wsudoPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
      FILE_ATTRIBUTE_NORMAL, nullptr
    );
    if (hFile == INVALID_HANDLE_VALUE) {
      log::error(L"Couldn't create '{}'.", wsudoPath);
      RETURN_LAST_ERROR();
    }
    WSUDO_SCOPE_EXIT { CloseHandle(hFile); };

    DWORD bytesWritten;
    char configText[] =
      "# Time to wait for the first connection\n"
      "wait-timeout 30\n"
      "\n"
      "# Time for a session to stay active\n"
      "session-timeout 300\n";
    if (!WriteFile(hFile, configText, WSUDO_ARRAYSIZE(configText) - 1,
                   &bytesWritten, nullptr)) {
      log::error(L"Failed to write default config to '{}'.", wsudoPath);
      RETURN_LAST_ERROR();
    }
  }

  return S_OK;
}

bool wsudo::uninstallService() {
  // On error we want to just keep going to do as much of the uninstall as
  // possible.
  bool success = true;

  auto regStatus = RegDeleteTreeW(
    HKEY_LOCAL_MACHINE,
    L"SYSTEM\\CurrentControlSet\\Services\\EventLog\\wsudo"
  );
  if (regStatus != ERROR_SUCCESS) {
    log::error("Couldn't delete registry key "
               "'HKLM\\SYSTEM\\CurrentControlSet\\Services\\EventLog\\wsudo'");
    success = false;
  }

  auto scManager = OpenSCManagerW(nullptr, nullptr, STANDARD_RIGHTS_WRITE);
  if (scManager) {
    WSUDO_SCOPE_EXIT { CloseServiceHandle(scManager); };
    auto service = OpenServiceW(scManager, L"wsudo", SERVICE_STOP | DELETE);
    if (service) {
      WSUDO_SCOPE_EXIT { CloseServiceHandle(service); };
      SERVICE_STATUS serviceStatus{};
      if (!ControlService(service, SERVICE_CONTROL_STOP, &serviceStatus)) {
        if (GetLastError() != ERROR_SERVICE_NOT_ACTIVE) {
          log::error("Couldn't stop service 'wsudo'.");
          success = false;
        }
      }
      if (!DeleteService(service)) {
        log::error("Couldn't delete service 'wsudo'.");
        success = false;
      }
    } else {
      log::error("Couldn't open service 'wsudo'.");
      success = false;
    }
  } else {
    log::error("Couldn't open service manager.");
    success = false;
  }

  auto groupDelStatus = NetLocalGroupDel(nullptr, L"wsudoers");
  if (groupDelStatus != NERR_Success) {
    log::error("Couldn't delete group 'wsudoers'.");
    success = false;
  }

  wchar_t *appDataPath;
  auto hr = SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr,
                                 &appDataPath);
  if (FAILED(hr)) {
    log::error("Couldn't get ProgramData path.");
    success = false;
  }
  std::wstring wsudoPath{appDataPath};
  WSUDO_SCOPE_EXIT { CoTaskMemFree(appDataPath); };

  wsudoPath.append(L"\\wsudo\\wsudo.conf");
  if (!DeleteFile(wsudoPath.c_str())) {
    log::error(L"Couldn't delete '{}'.", wsudoPath);
    success = false;
  }

  wsudoPath = appDataPath;
  wsudoPath.append(L"\\wsudo");
  if (!RemoveDirectory(wsudoPath.c_str())) {
    log::error(L"Couldn't remove directory '{}'.", wsudoPath);
    success = false;
  }

  return success;
}

HRESULT wsudo::clearEventLog() {
  HANDLE eventLog = OpenEventLog(nullptr, L"wsudo");
  if (eventLog == nullptr) {
    log::error("Could not open event log.");
    RETURN_WIN32(GetLastError());
  }
  WSUDO_SCOPE_EXIT { CloseEventLog(eventLog); };

  if (!ClearEventLog(eventLog, nullptr)) {
    log::error("Could not clear event log.");
    RETURN_WIN32(GetLastError());
  }

  return S_OK;
}
