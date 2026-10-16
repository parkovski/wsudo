#include "wsudo/wsudo.h"
#include "wsudo/server.h"
#include <Lm.h>

#pragma comment(lib, "Netapi32.lib")

HRESULT wsudo::installService() {
  HKEY hKey;
  LSTATUS status;
  status = RegCreateKeyExW(
    HKEY_LOCAL_MACHINE,
    L"SYSTEM\\CurrentControlSet\\Services\\EventLog\\wsudo\\wsudo", 0, nullptr,
    REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr
  );
  if (status != ERROR_SUCCESS) {
    log::error("Could not open registry.");
    return HRESULT_FROM_WIN32(status);
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
    return HRESULT_FROM_WIN32(status);
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
    return HRESULT_FROM_WIN32(status);
  }

  DWORD pathSize = MAX_PATH, copied;
  wchar_t *path = new wchar_t[pathSize];
  do {
    copied = GetModuleFileName(nullptr, path, pathSize);
    if (copied == pathSize && GetLastError() == ERROR_INSUFFICIENT_BUFFER)
    {
      delete[] path;
      pathSize *= 2;
      path = new wchar_t[pathSize];
    } else if (copied == 0) {
      return HRESULT_FROM_WIN32(GetLastError());
    } else {
      break;
    }
  } while (true);
  auto escapedPath = escapeCommandLineArg(std::wstring_view{path, copied});
  delete[] path;

  auto scManager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
  if (!scManager) {
    log::error("Could not open service manager.");
    return HRESULT_FROM_WIN32(GetLastError());
  }
  WSUDO_SCOPE_EXIT { CloseServiceHandle(scManager); };

  auto service = CreateServiceW(
    scManager, L"wsudo", L"wsudo", /*SERVICE_ALL_ACCESS*/ 0,
    SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL,
    escapedPath.c_str(), nullptr, nullptr, nullptr, nullptr, nullptr
  );
  if (!service) {
    log::error("Could not create service.");
    return HRESULT_FROM_WIN32(GetLastError());
  }
  WSUDO_SCOPE_EXIT { CloseServiceHandle(service); };
  log::info("Created service wsudo.");

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
    return HRESULT_FROM_WIN32(grpResult);
  }
  log::info("Created group wsudoers.");

  return S_OK;
}

HRESULT wsudo::uninstallService() {
  // On error we want to just keep going to do as much of the uninstall as
  // possible.

  RegDeleteTreeW(
    HKEY_LOCAL_MACHINE,
    L"SYSTEM\\CurrentControlSet\\Services\\EventLog\\wsudo"
  );

  auto scManager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
  if (scManager) {
    WSUDO_SCOPE_EXIT { CloseServiceHandle(scManager); };
    auto service = OpenServiceW(scManager, L"wsudo", SERVICE_STOP | DELETE);
    if (service) {
      WSUDO_SCOPE_EXIT { CloseServiceHandle(service); };
      SERVICE_STATUS serviceStatus{};
      ControlService(service, SERVICE_CONTROL_STOP, &serviceStatus);
      DeleteService(service);
    }
  }

  NetLocalGroupDel(nullptr, L"wsudoers");

  return S_OK;
}

HRESULT wsudo::clearEventLog() {
  HANDLE eventLog = OpenEventLog(nullptr, L"wsudo");
  if (eventLog == nullptr) {
    log::error("Could not open event log.");
    return HRESULT_FROM_WIN32(GetLastError());
  }
  WSUDO_SCOPE_EXIT { CloseEventLog(eventLog); };

  if (!ClearEventLog(eventLog, nullptr)) {
    log::error("Could not clear event log.");
    return HRESULT_FROM_WIN32(GetLastError());
  }

  log::info("Event log cleared.");

  return S_OK;
}
