#include "wsudo/wsudo.h"

#include <spdlog/sinks/stdout_color_sinks.h>

#include <cstring>

#define CATCH_CONFIG_RUNNER
#include <catch2/catch_all.hpp>

int main(int argc, char *argv[]) {
  wsudo::log::g_outLogger = spdlog::stdout_color_mt("wsudo.out");
  wsudo::log::g_errLogger = spdlog::stderr_color_mt("wsudo.err");

  // Use --spdlog=n (reverse of level values):
  // 0=off
  // 1=critical
  // 2=error
  // 3=warning
  // 4=info
  // 5=debug
  // 6=trace
  spdlog::level::level_enum level{spdlog::level::trace};
  char empty[1] = { 0 };
  for (int i = 0; i < argc; ++i) {
    auto len = strlen(argv[i]);
    auto len_spdlog_eq = strlen("--spdlog=");
    if (!strncmp(argv[i], "--spdlog=", len_spdlog_eq)
        && len == len_spdlog_eq + 1
        && argv[i][len_spdlog_eq] >= '0' && argv[i][len_spdlog_eq] <= '6') {
      level = (spdlog::level::level_enum)(6 - (argv[i][len_spdlog_eq] - '0'));
      argv[i] = empty;
    }
  }
  spdlog::set_level(level);

  Catch::Session session;
  int result = session.run(argc, argv);

  return result;
}
