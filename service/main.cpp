#include "stdo/stdo.h"
#include "stdo/winsupport.h"
#include "stdo/ntapi.h"

#include <fmt/format.h>
#include <Psapi.h>
#include <cstring>
#include <string>
#include <iostream>
#include <system_error>

using namespace stdo;

/// Convert exe + args to a single string, quoted if the exe contains spaces.
std::wstring fullCommandLine(const wchar_t *exeName, const wchar_t *args) {
  size_t exelen = wcslen(exeName);
  size_t arglen = args ? wcslen(args) : 0;
  bool hasSpace = !!wcschr(exeName, L' ');
  size_t lenfull = exelen;
  if (hasSpace) {
    lenfull += 2;
  }
  if (arglen) {
    lenfull += arglen + 1;
  }
  std::wstring argsFull;
  argsFull.reserve(lenfull);
  if (hasSpace) {
    argsFull.push_back(L'\"');
    argsFull.append(exeName);
    argsFull.push_back(L'\"');
  } else {
    argsFull.append(exeName);
  }
  if (arglen) {
    argsFull.push_back(L' ');
    argsFull.append(args);
  }

  return argsFull;
}

/// Duplicate this process' token into a new primary token.
DWORD getPrimaryToken(HStdHandle &token) {
  HStdHandle currentToken;
  log::trace("Creating primary token from current process.");
  if (
    !OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &currentToken) ||
    !DuplicateTokenEx(
      currentToken, TOKEN_ALL_ACCESS, nullptr, SecurityImpersonation,
      TokenPrimary, &token
    )
  )
  {
    log::error("Creating primary token failed.");
    return GetLastError();
  }

  return ERROR_SUCCESS;
}

DWORD findNamedModule(HANDLE process, const wchar_t *modName, HMODULE *module) {
  // TODO: Hack
  HMODULE modules[100];
  DWORD bytesNeeded;
  log::trace("Enumerating remote process modules.");
  *module = nullptr;
  if (!EnumProcessModules(process, modules, sizeof(modules), &bytesNeeded)) {
    return GetLastError();
  }
  if (bytesNeeded > 100 * sizeof(HMODULE)) {
    bytesNeeded = 100;
  } else {
    bytesNeeded /= sizeof(HMODULE);
  }
  int moduleIndex = -1;

  log::trace("Searching for target module.");
  for (unsigned i = 0; i < bytesNeeded; ++i) {
    wchar_t filename[512];
    if (!GetModuleFileNameExW(process, modules[i], filename, sizeof(filename)/sizeof(filename[0]))) {
      log::warn("module name missing: 0x{:X}", (size_t)modules[i]);
    } else if (wcsstr(filename, modName)) {
      log::trace("found module {}", to_utf8(filename));
      moduleIndex = i;
      break;
    }
  }
  if (moduleIndex == -1) {
    return ERROR_MOD_NOT_FOUND;
  }
  *module = modules[moduleIndex];
  return ERROR_SUCCESS;
}

DWORD childProcess(DWORD pid, const wchar_t *exeName, const wchar_t *args) {
  union {
    DWORD win32;
    HRESULT hr;
    NTSTATUS nt;
  } err;

  auto ntdll = LinkedModule{L"ntdll.dll"};
  auto RtlNtStatusToDosError =
    ntdll.get<nt::RtlNtStatusToDosError_t>("RtlNtStatusToDosError");
  auto NtQueryInformationProcess =
    ntdll.get<nt::NtQueryInformationProcess_t>("NtQueryInformationProcess");
  auto NtSetInformationProcess =
    ntdll.get<nt::NtSetInformationProcess_t>("NtSetInformationProcess");

  log::trace("Opening remote process with PROCESS_ALL_ACCESS.");
  HStdHandle process;
  if (!(process = OpenProcess(PROCESS_ALL_ACCESS, false, pid))) {
    return GetLastError();
  }

  HStdHandle token;
  err.win32 = getPrimaryToken(token);
  if (err.win32) { return err.win32; }

  PROCESS_BASIC_INFORMATION pbi;
  ULONG pbilen;
  log::trace("Finding remote PEB address with NtQueryInformationProcess.");
  err.nt = NtQueryInformationProcess(
    process, ProcessBasicInformation, &pbi, sizeof(PROCESS_BASIC_INFORMATION),
    &pbilen
  );
  if (err.nt >= 0x80000000) {
    return RtlNtStatusToDosError(err.nt);
  }

  PVOID pbaseAddress;
  err.win32 = findNamedModule(process, L"stdo.exe", reinterpret_cast<HMODULE *>(&pbaseAddress));
  if (err.win32) { return err.win32; }

  PEB peb;
  nt::RTL_USER_PROCESS_PARAMETERS processParameters;
  SIZE_T bytesRead;
  log::trace("Reading PEB from remote process.");
  if (
    !ReadProcessMemory(process, pbi.PebBaseAddress, &peb, sizeof(PEB), &bytesRead) ||
    bytesRead < sizeof(PEB)
  )
  {
    return GetLastError();
  }
  log::trace("Reading RTL_USER_PROCESS_PARAMETERS from remote process.");
  if (
    !ReadProcessMemory(
      process, peb.ProcessParameters, nt::alt(&processParameters),
      sizeof(nt::RTL_USER_PROCESS_PARAMETERS), &bytesRead
    ) ||
    bytesRead < sizeof(nt::RTL_USER_PROCESS_PARAMETERS)
  )
  {
    return GetLastError();
  }

  STARTUPINFOEXW si;
  RtlZeroMemory(&si, sizeof(STARTUPINFOEXW));
  si.StartupInfo.cb = sizeof(STARTUPINFOEXW);
  log::trace("stdin  = 0x{:X}", (size_t)processParameters.StandardInput);
  log::trace("stdout = 0x{:X}", (size_t)processParameters.StandardOutput);
  log::trace("stderr = 0x{:X}", (size_t)processParameters.StandardError);
  si.StartupInfo.hStdInput = processParameters.StandardInput;
  si.StartupInfo.hStdOutput = processParameters.StandardOutput;
  si.StartupInfo.hStdError = processParameters.StandardError;
  si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;

  SIZE_T attrListSize;
  InitializeProcThreadAttributeList(nullptr, 1, 0, &attrListSize);
  si.lpAttributeList =
    (LPPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(), 0, attrListSize);
  if (!si.lpAttributeList) {
    return GetLastError();
  }
  STDO_SCOPEEXIT { HeapFree(GetProcessHeap(), 0, si.lpAttributeList); };

  log::trace("Setting remote process as parent.");
  if (!InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attrListSize)) {
    return GetLastError();
  }
  STDO_SCOPEEXIT { DeleteProcThreadAttributeList(si.lpAttributeList); };

  if (!UpdateProcThreadAttribute(
    si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PARENT_PROCESS, &process, sizeof(HANDLE), nullptr, nullptr
  ))
  {
    return GetLastError();
  }

  log::trace("Creating suspended remote child process.");
  PROCESS_INFORMATION pi;
  if (!CreateProcessW(
    exeName, fullCommandLine(exeName, args).data(), nullptr, nullptr, true,
    CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED | CREATE_PRESERVE_CODE_AUTHZ_LEVEL,
    nullptr, nullptr, &si.StartupInfo, &pi
  ))
  {
    return GetLastError();
  }

  STDO_SCOPEEXIT {
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
  };

  nt::PROCESS_ACCESS_TOKEN processAccessToken{token, pi.hThread};

  log::trace("Assigning elevated token to child process.");
  err.nt = NtSetInformationProcess(
    pi.hProcess, nt::ProcessAccessToken, &processAccessToken,
    sizeof(processAccessToken)
  );
  if (err.nt >= 0x80000000) {
    return RtlNtStatusToDosError(err.nt);
  }

  log::trace("Resuming child process.");
  ResumeThread(pi.hThread);

  return ERROR_SUCCESS;
}

std::shared_ptr<spdlog::logger> stdo::log::g_outLogger;
std::shared_ptr<spdlog::logger> stdo::log::g_errLogger;

int wmain(int argc, wchar_t *argv[]) {
  stdo::log::g_outLogger = spdlog::stdout_color_mt("stdo.out");
  stdo::log::g_errLogger = spdlog::stderr_color_mt("stdo.err");
  log::g_errLogger->set_level(spdlog::level::warn);
  log::g_outLogger->set_level(spdlog::level::trace);
  STDO_SCOPEEXIT { spdlog::drop_all(); };

  log::info("Hello from server");
  DWORD pid;
  if (argc == 2) {
    wchar_t *end = argv[1] + wcslen(argv[1]);
    pid = wcstoul(argv[1], &end, 10);
  } else {
    fmt::print("Process ID> ");
    std::string s;
    std::getline(std::cin, s);
    pid = std::stoul(s);
  }

  auto err = childProcess(pid, L"C:\\Windows\\System32\\cmd.exe", nullptr);
  // auto err = childProcess(pid, L"C:\\Windows\\System32\\whoami.exe", L"/all");
  if (err != ERROR_SUCCESS) {
    char buf[1024];
    FormatMessageA(
      FORMAT_MESSAGE_FROM_SYSTEM, nullptr, err, 0, buf,
      sizeof(buf)/sizeof(buf[0]), nullptr
    );
    log::error("{}", buf);
  } else {
    log::info("Success");
  }
  return 0;
}
