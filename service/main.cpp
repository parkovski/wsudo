#include <fmt/format.h>
#include <Windows.h>
#include <Psapi.h>
#include <cstring>
#include <string>
#include <iostream>

void remoteThread() {
  fmt::print("Process ID> ");
  std::string s;
  std::getline(std::cin, s);
  unsigned long long pid = std::stoull(s);
  fmt::print("Function address> ");
  std::getline(std::cin, s);
  unsigned long long addr = std::stoull(s);

  HANDLE process = OpenProcess(PROCESS_ALL_ACCESS, false, (DWORD)pid);
  DWORD threadId;
  HANDLE thread = CreateRemoteThread(process, nullptr, 0, (LPTHREAD_START_ROUTINE)addr, nullptr, CREATE_SUSPENDED, &threadId);
  fmt::print("Enter to run thread\n");
  std::getline(std::cin, s);
  ResumeThread(thread);
  CloseHandle(thread);
  CloseHandle(process);
}

typedef struct _STRING {
  USHORT Length;
  USHORT MaximumLength;
  PCHAR  Buffer;
} STRING, OEM_STRING, *PSTRING;

typedef struct _LSA_UNICODE_STRING {
  USHORT Length;
  USHORT MaximumLength;
  PWSTR  Buffer;
} LSA_UNICODE_STRING, *PLSA_UNICODE_STRING, UNICODE_STRING, *PUNICODE_STRING;

typedef struct _CURDIR
{
    UNICODE_STRING DosPath;
    HANDLE Handle;
} CURDIR, *PCURDIR;

typedef struct _RTL_DRIVE_LETTER_CURDIR
{
    USHORT Flags;
    USHORT Length;
    ULONG TimeStamp;
    STRING DosPath;
} RTL_DRIVE_LETTER_CURDIR, *PRTL_DRIVE_LETTER_CURDIR;

#define RTL_MAX_DRIVE_LETTERS 32
typedef struct _RTL_USER_PROCESS_PARAMETERS
{
    ULONG MaximumLength;
    ULONG Length;

    ULONG Flags;
    ULONG DebugFlags;

    HANDLE ConsoleHandle;
    ULONG ConsoleFlags;
    HANDLE StandardInput;
    HANDLE StandardOutput;
    HANDLE StandardError;

    CURDIR CurrentDirectory;
    UNICODE_STRING DllPath;
    UNICODE_STRING ImagePathName;
    UNICODE_STRING CommandLine;
    PVOID Environment;

    ULONG StartingX;
    ULONG StartingY;
    ULONG CountX;
    ULONG CountY;
    ULONG CountCharsX;
    ULONG CountCharsY;
    ULONG FillAttribute;

    ULONG WindowFlags;
    ULONG ShowWindowFlags;
    UNICODE_STRING WindowTitle;
    UNICODE_STRING DesktopInfo;
    UNICODE_STRING ShellInfo;
    UNICODE_STRING RuntimeData;
    RTL_DRIVE_LETTER_CURDIR CurrentDirectories[RTL_MAX_DRIVE_LETTERS];

    ULONG_PTR EnvironmentSize;
    ULONG_PTR EnvironmentVersion;
    PVOID PackageDependencyData;
    ULONG ProcessGroupId;
    ULONG LoaderThreads;
} RTL_USER_PROCESS_PARAMETERS, *PRTL_USER_PROCESS_PARAMETERS;

typedef struct _PEB
{
    BOOLEAN InheritedAddressSpace;
    BOOLEAN ReadImageFileExecOptions;
    BOOLEAN BeingDebugged;
    union
    {
        BOOLEAN BitField;
        struct
        {
            BOOLEAN ImageUsesLargePages : 1;
            BOOLEAN IsProtectedProcess : 1;
            BOOLEAN IsImageDynamicallyRelocated : 1;
            BOOLEAN SkipPatchingUser32Forwarders : 1;
            BOOLEAN IsPackagedProcess : 1;
            BOOLEAN IsAppContainer : 1;
            BOOLEAN IsProtectedProcessLight : 1;
            BOOLEAN IsLongPathAwareProcess : 1;
        };
    };

    HANDLE Mutant;

    PVOID ImageBaseAddress;
    PVOID Ldr;
    PRTL_USER_PROCESS_PARAMETERS ProcessParameters;
    PVOID SubSystemData;
} PEB, *PPEB;

typedef struct _PROCESS_BASIC_INFORMATION {
    PVOID Reserved1;
    PPEB PebBaseAddress;
    PVOID Reserved2[2];
    ULONG_PTR UniqueProcessId;
    PVOID Reserved3;
} PROCESS_BASIC_INFORMATION;

DWORD childProcess(DWORD pid, const wchar_t *exeName, const wchar_t *args) {
  STARTUPINFOEXW si{};
  PROCESS_INFORMATION pi{};
  HANDLE process{};
  HANDLE myToken{};
  HANDLE token{};
  auto ntdll = GetModuleHandleW(L"ntdll.dll");
  NTSTATUS ntstatus;
  auto RtlNtStatusToDosError =
    reinterpret_cast<ULONG (* WINAPI)(NTSTATUS)>(
      GetProcAddress(ntdll, "RtlNtStatusToDosError")
    );

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

  using std::cout;

  cout << "OpenProcess(PROCESS_ALL_ACCESS, false, pid);\n";
  if (!(process = OpenProcess(PROCESS_ALL_ACCESS, false, pid))) {
    goto error;
  }
  cout << "OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &myToken);\n";
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &myToken)) {
    goto error;
  }
  cout << "DuplicateTokenEx(myToken, TOKEN_ALL_ACCESS, nullptr, SecurityImpersonation, TokenPrimary, &token);\n";
  if (!DuplicateTokenEx(
    myToken, TOKEN_ALL_ACCESS, nullptr, SecurityImpersonation, TokenPrimary, &token
  ))
  {
    goto error;
  }

  enum PROCESSINFOCLASS { ProcessBasicInformation = 0 };

  auto NtQueryInformationProcess =
    reinterpret_cast<NTSTATUS (WINAPI *)(HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG)>(
      GetProcAddress(ntdll, "NtQueryInformationProcess")
    );
  PROCESS_BASIC_INFORMATION pbi;
  ULONG pbilen;
  cout << "NtQueryInformationProcess(process, ProcessBasicInformation, &pbi, sizeof(PROCESS_BASIC_INFORMATION), &pbilen);\n";
  ntstatus = NtQueryInformationProcess(
    process, ProcessBasicInformation, &pbi, sizeof(PROCESS_BASIC_INFORMATION),
    &pbilen
  );
  if (ntstatus >= 0x80000000) {
    SetLastError(RtlNtStatusToDosError(ntstatus));
    goto error;
  }

  // TODO: Hack
  HMODULE modules[100];
  DWORD bytesNeeded;
  cout << "EnumProcessModules(process, modules, sizeof(modules), &bytesNeeded);\n";
  if (!EnumProcessModules(process, modules, sizeof(modules), &bytesNeeded)) {
    goto error;
  }
  if (bytesNeeded > 100 * sizeof(HMODULE)) {
    bytesNeeded = 100;
  } else {
    bytesNeeded /= sizeof(HMODULE);
  }
  int moduleIndex = -1;

  cout << "GetModuleFileNameW(modules[i], filename, sizeof(filename)/sizeof(filename[0]));\n";
  for (unsigned i = 0; i < bytesNeeded; ++i) {
    wchar_t filename[512];
    if (!GetModuleFileNameExW(process, modules[i], filename, sizeof(filename)/sizeof(filename[0]))) {
      cout << " > ***MISSING*** (" << modules[i] << ")\n";
    } else {
      std::wcout << L" > " << filename << L"\n";
    }
    if (wcsstr(filename, L"stdo.exe")) {moduleIndex = i; break;}
  }
  if (moduleIndex == -1) {
    goto error;
  }
  PVOID pbaseAddress = modules[moduleIndex];

  PEB peb;
  RTL_USER_PROCESS_PARAMETERS processParameters;
  SIZE_T bytesRead;
  cout << "ReadProcessMemory(PEB);\n";
  if (
    !ReadProcessMemory(process, pbi.PebBaseAddress, &peb, sizeof(PEB), &bytesRead) ||
    bytesRead < sizeof(PEB)
  )
  {
    goto error;
  }
  cout << "ReadProcessMemory(RTL_USE_PROCESS_PARAMETERS);\n";
  if (
    !ReadProcessMemory(process, peb.ProcessParameters, &processParameters,
                       sizeof(RTL_USER_PROCESS_PARAMETERS), &bytesRead) ||
    bytesRead < sizeof(RTL_USER_PROCESS_PARAMETERS)
  )
  {
    goto error;
  }

  RtlZeroMemory(&si, sizeof(STARTUPINFOEXW));
  si.StartupInfo.cb = sizeof(STARTUPINFOEXW);
  cout << " > stdin  = " << processParameters.StandardInput << "\n";
  cout << " > stdout = " << processParameters.StandardOutput << "\n";
  cout << " > stderr = " << processParameters.StandardError << "\n";
  si.StartupInfo.hStdInput = processParameters.StandardInput;
  si.StartupInfo.hStdOutput = processParameters.StandardOutput;
  si.StartupInfo.hStdError = processParameters.StandardError;
  // si.StartupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  // si.StartupInfo.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
  // si.StartupInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;

  SIZE_T attrListSize;
  InitializeProcThreadAttributeList(nullptr, 1, 0, &attrListSize);
  si.lpAttributeList =
    (LPPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(), 0, attrListSize);

  if (!InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attrListSize)) {
    goto error;
  }

  if (!UpdateProcThreadAttribute(
    si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PARENT_PROCESS, &process, sizeof(HANDLE), nullptr, nullptr
  ))
  {
    goto error;
  }

  cout << "CreateProcessW(...);\n";
  if (!CreateProcessW(
    exeName, argsFull.data(), nullptr, nullptr, true,
    CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED | CREATE_PRESERVE_CODE_AUTHZ_LEVEL,
    nullptr, nullptr, &si.StartupInfo, &pi
  ))
  {
    goto error;
  }

  auto NtSetInformationProcess =
    reinterpret_cast<NTSTATUS (* WINAPI)(HANDLE, int, PVOID, ULONG)>(
      GetProcAddress(ntdll, "NtSetInformationProcess")
    );
  struct {
    HANDLE Token;
    HANDLE Thread;
  } processAccessToken{token, pi.hThread};

  DWORD result;

  cout << "NtSetInformationProcess(...);\n";
  ntstatus = NtSetInformationProcess(pi.hProcess, 9, &processAccessToken, sizeof(processAccessToken));
  if (ntstatus >= 0x80000000) {
    SetLastError(RtlNtStatusToDosError(ntstatus));
    goto error;
  }

  cout << "ResumeThread\n";
  ResumeThread(pi.hThread);

  result = S_OK;
  goto cleanup;
error:
  result = GetLastError();
cleanup:
  if (myToken) {
    CloseHandle(myToken);
  }
  if (token) {
    CloseHandle(token);
  }
  if (process) {
    CloseHandle(process);
  }
  if (si.lpAttributeList) {
    DeleteProcThreadAttributeList(si.lpAttributeList);
    HeapFree(GetProcessHeap(), 0, si.lpAttributeList);
  }
  if (pi.hProcess) {
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
  }
  return result;
}

int wmain(int argc, wchar_t *argv[]) {
  fmt::print("Hello from server\n");
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
  if (err != S_OK) {
    wchar_t buf[1024];
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, err, 0, buf, 1024 / 2, nullptr);
    wprintf(L"Error: %ls\n", buf);
  } else {
    fmt::print("Success!\n");
  }
  return 0;
}
