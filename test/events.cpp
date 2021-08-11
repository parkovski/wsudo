#include "wsudo/events.h"

#include <catch2/catch_all.hpp>

TEST_CASE("EventListener chooses correctly.", "[events]") {
  using namespace wsudo::events;

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
