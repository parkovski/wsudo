#include "wsudo/wsudo.h"

namespace wsudo {

namespace log {
  std::shared_ptr<spdlog::logger> g_outLogger;
  std::shared_ptr<spdlog::logger> g_errLogger;
}

const wchar_t *const PipeFullPath = L"\\\\.\\pipe\\wsudo_token_server";

namespace msg {
  namespace client {
    const char *const Credential = "CRED";
    const char *const Bless = "BLES";
  }

  namespace server {
    const char *const Success = "SUCC";
    const char *const InvalidMessage = "MESG";
    const char *const InternalError = "INTE";
    const char *const AccessDenied = "DENY";
  }
} // namespace msg

} // namespace wsudo
