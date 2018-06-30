#include "stdo/stdo.h"

namespace stdo {

namespace log {
  std::shared_ptr<spdlog::logger> g_outLogger;
  std::shared_ptr<spdlog::logger> g_errLogger;
}

const wchar_t *PipeName = L"stdo_tokreq";
const wchar_t *PipeFullPath = L"\\\\.\\pipe\\stdo_tokreq";

const char *MsgHeaderCredential = "CRED";
const char *MsgHeaderBless = "BLES";

const char *SMsgHeaderSuccess = "SUCC";
const char *SMsgHeaderInvalidMessage = "MESG";
const char *SMsgHeaderInternalError = "INTE";
const char *SMsgHeaderAccessDenied = "DENY";

} // namespace stdo

