#pragma once

namespace wsudo {

struct ServerConfig {
  unsigned waitTimeout;
  unsigned sessionTimeout;

  ServerConfig() {
    setDefaults();
  }

  void setDefaults() noexcept;

  bool parse() noexcept;
};

} // namespace wsudo
