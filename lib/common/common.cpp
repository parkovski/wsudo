#include "wsudo/wsudo.h"

namespace wsudo {

namespace log {
  std::shared_ptr<spdlog::logger> g_outLogger;
  std::shared_ptr<spdlog::logger> g_errLogger;
}

const wchar_t *const PipeFullPath = L"\\\\.\\pipe\\wsudo_token_server";

} // namespace wsudo
