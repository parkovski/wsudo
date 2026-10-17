#include "wsudo/wsudo.h"
#include "wsudo/server.h"
#include <Lm.h>
#define SECURITY_WIN32
#include <Security.h>
#include <wil/result_macros.h>

#pragma comment(lib, "Netapi32.lib")

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

  return S_OK;
}

bool wsudo::uninstallService() {
  // On error we want to just keep going to do as much of the uninstall as
  // possible.
  bool success;

  success = RegDeleteTreeW(
    HKEY_LOCAL_MACHINE,
    L"SYSTEM\\CurrentControlSet\\Services\\EventLog\\wsudo"
  ) == ERROR_SUCCESS;

  auto scManager = OpenSCManagerW(nullptr, nullptr, STANDARD_RIGHTS_WRITE);
  if (scManager) {
    WSUDO_SCOPE_EXIT { CloseServiceHandle(scManager); };
    auto service = OpenServiceW(scManager, L"wsudo", SERVICE_STOP | DELETE);
    if (service) {
      WSUDO_SCOPE_EXIT { CloseServiceHandle(service); };
      SERVICE_STATUS serviceStatus{};
      ControlService(service, SERVICE_CONTROL_STOP, &serviceStatus);
      DeleteService(service);
    } else {
      success = false;
    }
  } else {
    success = false;
  }

  success &= NetLocalGroupDel(nullptr, L"wsudoers") == NERR_Success;

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

  log::info("Event log cleared.");

  return S_OK;
}
