#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/logger.h>

namespace wsudo::log {

// Logger that prints to stdout.
extern std::shared_ptr<spdlog::logger> g_outLogger;

// Logger that prints to stderr.
extern std::shared_ptr<spdlog::logger> g_errLogger;

/// Print to stdout with no prefix.
template<typename... Args>
static inline void print(const char *fmt, Args &&...args)
{ fmt::print(stdout, fmt, std::forward<Args>(args)...); }

/// Print to stderr with no prefix.
template<typename... Args>
static inline void eprint(const char *fmt, Args &&...args)
{ fmt::print(stderr, fmt, std::forward<Args>(args)...); }

/// Trace logger.
template<typename... Args>
static inline void trace(const char *fmt, Args &&...args)
{ g_outLogger->trace(fmt, std::forward<Args>(args)...); }

/// Debug logger.
template<typename... Args>
static inline void debug(const char *fmt, Args &&...args)
{ g_outLogger->debug(fmt, std::forward<Args>(args)...); }

/// Info logger.
template<typename... Args>
static inline void info(const char *fmt, Args &&...args)
{ g_outLogger->info(fmt, std::forward<Args>(args)...); }

/// Warning logger.
template<typename... Args>
static inline void warn(const char *fmt, Args &&...args)
{ g_errLogger->warn(fmt, std::forward<Args>(args)...); }

/// Error logger.
template<typename... Args>
static inline void error(const char *fmt, Args &&...args)
{ g_errLogger->error(fmt, std::forward<Args>(args)...); }

/// Critical logger.
template<typename... Args>
static inline void critical(const char *fmt, Args &&...args)
{ g_errLogger->critical(fmt, std::forward<Args>(args)...); }

#ifndef SPDLOG_WCHAR_TO_UTF8_SUPPORT
#error "Requires spdlog wchar_t logging feature"
#endif

/// Print to stdout with no prefix (wchar_t version).
template<typename... Args>
static inline void print(const wchar_t *fmt, Args &&...args)
{ fmt::print(stdout, fmt, std::forward<Args>(args)...); }

/// Print to stderr with no prefix (wchar_t version).
template<typename... Args>
static inline void eprint(const wchar_t *fmt, Args &&...args)
{ fmt::print(stderr, fmt, std::forward<Args>(args)...); }

/// Trace logger (wchar_t version).
template<typename... Args>
static inline void trace(const wchar_t *fmt, Args &&...args)
{ g_outLogger->trace(fmt, std::forward<Args>(args)...); }

/// Debug logger (wchar_t version).
template<typename... Args>
static inline void debug(const wchar_t *fmt, Args &&...args)
{ g_outLogger->debug(fmt, std::forward<Args>(args)...); }

/// Info logger (wchar_t version).
template<typename... Args>
static inline void info(const wchar_t *fmt, Args &&...args)
{ g_outLogger->info(fmt, std::forward<Args>(args)...); }

/// Warning logger (wchar_t version).
template<typename... Args>
static inline void warn(const wchar_t *fmt, Args &&...args)
{ g_errLogger->warn(fmt, std::forward<Args>(args)...); }

/// Error logger (wchar_t version).
template<typename... Args>
static inline void error(const wchar_t *fmt, Args &&...args)
{ g_errLogger->error(fmt, std::forward<Args>(args)...); }

/// Critical logger (wchar_t version).
template<typename... Args>
static inline void critical(const wchar_t *fmt, Args &&...args)
{ g_errLogger->critical(fmt, std::forward<Args>(args)...); }

} // namespace wsudo::log
