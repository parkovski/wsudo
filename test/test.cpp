#include "wsudo/wsudo.h"
#include "wsudo/events.h"

#include <spdlog/sinks/stdout_color_sinks.h>

#define CATCH_CONFIG_RUNNER
#include <catch.hpp>

using namespace wsudo;

int main(int argc, char *argv[]) {
  log::g_outLogger = spdlog::stdout_color_mt("wsudo.out");
  log::g_errLogger = spdlog::stderr_color_mt("wsudo.err");

  WSUDO_SCOPEEXIT { spdlog::drop_all(); };

  int result = Catch::Session().run(argc, argv);

  return result;
}

TEST_CASE("LogonUser", "[.logon]") {
  wchar_t username[256];
  wchar_t password[256];
  DWORD length;

  length = GetEnvironmentVariableW(L"WSUSER", username, sizeof(username));
  if (length == 0) {
    FAIL("Username (env WSUSER) not present.");
  } else if (length >= sizeof(username)) {
    FAIL("Username (env WSUSER) too long.");
  }

  length = GetEnvironmentVariableW(L"WSPASSWORD", password, sizeof(password));
  if (length == 0) {
    FAIL("Password (env WSPASSWORD) not present.");
  } else if (length >= sizeof(password)) {
    FAIL("Password (env WSPASSWORD) too long.");
  }

  HObject token;
  HLocalPtr<PSID> pSid;
  PVOID pProfileBuffer;
  DWORD profileLength;
  QUOTA_LIMITS quotaLimits;
  if (!LogonUserExW(username, L".", password, LOGON32_LOGON_NETWORK,
                    LOGON32_PROVIDER_DEFAULT, &token, &pSid, &pProfileBuffer,
                    &profileLength, &quotaLimits))
  {
    FAIL("LogonUserExW failed" << lastErrorString());
  }
  REQUIRE(!!token);
}

TEST_CASE("EventListener chooses correctly.", "[events]") {
  using namespace events;

  EventListener listener;
  FILETIME systemTime;
  int timerNr = 0;

  GetSystemTimeAsFileTime(&systemTime);

  // id = a number to identify the timer.
  // duration = time in milliseconds before the timer is triggered.
  auto addTimer = [&](int id, long duration) {
    HANDLE timer = CreateWaitableTimerW(nullptr, true, nullptr);

    union {
      FILETIME fileTime;
      LARGE_INTEGER dueTime;
    };
    fileTime = systemTime;
    // Timers use 100ns intervals, but we want ms.
    dueTime.QuadPart += duration * 10000;

    SetWaitableTimer(timer, &dueTime, 0, nullptr, nullptr, false);

    listener.emplace(timer, [id, &timerNr](EventListener &){
      timerNr = id;
      return EventStatus::Finished;
    });
  };

  // Timer order: 2, 3, 1.
  addTimer(1, 30);
  addTimer(2, 10);
  addTimer(3, 20);

  REQUIRE(listener.next() == EventStatus::Ok);
  REQUIRE(timerNr == 2);
  REQUIRE(listener.next() == EventStatus::Ok);
  REQUIRE(timerNr == 3);
  REQUIRE(listener.next() == EventStatus::Finished);
  REQUIRE(timerNr == 1);
}
