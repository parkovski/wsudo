#ifndef STDO_NTAPI_H
#define STDO_NTAPI_H

/**
 * This header contains parts of the NT API not in Windows.h. Some of the
 * structures are incomplete; in those cases we don't have any need for the
 * rest of them.
 */

#include <Windows.h>
#include <winternl.h>

namespace stdo::nt {

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

typedef struct _PROCESS_ACCESS_TOKEN {
    HANDLE Token;
    HANDLE Thread;
} PROCESS_ACCESS_TOKEN, *PPROCESS_ACCESS_TOKEN;

constexpr ::PROCESSINFOCLASS ProcessAccessToken = (::PROCESSINFOCLASS)9;

typedef ULONG (WINAPI *RtlNtStatusToDosError_t)(NTSTATUS);
typedef NTSTATUS (WINAPI *NtQueryInformationProcess_t)(
    HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG
);
typedef NTSTATUS (WINAPI *NtSetInformationProcess_t)(
    HANDLE, PROCESSINFOCLASS, PVOID, ULONG
);

namespace detail {

template<typename> struct AltStruct;
template<> struct AltStruct<::PEB> { using type = PEB; };
template<> struct AltStruct<PEB> { using type = ::PEB; };
template<> struct AltStruct<::RTL_USER_PROCESS_PARAMETERS> {
    using type = RTL_USER_PROCESS_PARAMETERS;
};
template<> struct AltStruct<RTL_USER_PROCESS_PARAMETERS> {
    using type = ::RTL_USER_PROCESS_PARAMETERS;
};

}

/// Since these APIs are not complete in the standard headers,
/// these functions allow us to easily switch between alternate definitions.
template<typename S>
typename detail::AltStruct<S>::type *alt(S *s) {
    return reinterpret_cast<typename detail::AltStruct<S>::type *>(s);
}
template<typename S>
const typename detail::AltStruct<S>::type *alt(const S *s) {
    return reinterpret_cast<const typename detail::AltStruct<S>::type *>(s);
}
template<typename S>
typename detail::AltStruct<S>::type &alt(S &s) {
    return reinterpret_cast<typename detail::AltStruct<S>::type &>(s);
}
template<typename S>
const typename detail::AltStruct<S>::type &alt(const S &s) {
    return reinterpret_cast<const typename detail::AltStruct<S>::type &>(s);
}

} // namespace stdo::nt

#endif // STDO_NTAPI_H
