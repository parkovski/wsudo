#include "wsudo/client.h"

#include <spdlog/sinks/stdout_color_sinks.h>

using namespace wsudo;

int wmain(int argc, wchar_t *argv[]) {
  log::g_outLogger = spdlog::stdout_color_mt("wsudo.out");
  log::g_outLogger->set_level(spdlog::level::trace);
  log::g_errLogger = spdlog::stderr_color_mt("wsudo.err");
  log::g_errLogger->set_level(spdlog::level::warn);
  spdlog::set_pattern("%^[%l]%$ %v");

  if (argc < 2) {
    log::eprint("Usage: wsudo <program> <args>\n");
    return ClientExitInvalidUsage;
  }

  std::wstring pipename{PipeFullPath};
  Client client(pipename, argc - 1, argv + 1);
  log::debug("Client initialized");
  return (int)client();
}
