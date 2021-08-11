#include "wsudo/wsudo.h"

#include <spdlog/sinks/stdout_color_sinks.h>

#define CATCH_CONFIG_RUNNER
#include <catch2/catch_all.hpp>

using namespace wsudo;

int main(int argc, char *argv[]) {
  log::g_outLogger = spdlog::stdout_color_mt("wsudo.out");
  log::g_errLogger = spdlog::stderr_color_mt("wsudo.err");

  WSUDO_SCOPEEXIT { spdlog::drop_all(); };

  int result = Catch::Session().run(argc, argv);

  return result;
}
