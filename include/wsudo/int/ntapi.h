#if defined(WSUDO_NTAPI_H) || !defined(WSUDO_WSUDO_H)
#error "Do not include this file directly; use wsudo.h."
#else
#define WSUDO_NTAPI_H

/**
 * This header contains parts of the NT API not in Windows.h. Some of the
 * structures are incomplete; in those cases we don't have any need for the
 * rest of them.
 */

namespace wsudo::nt {

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

} // namespace wsudo::nt

#endif // WSUDO_NTAPI_H
