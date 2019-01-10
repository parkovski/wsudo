#include "wsudo/wsudo.h"

#define CATCH_CONFIG_MAIN
#include <catch.hpp>

using namespace wsudo;

static int add_one(int i) {
  return i + 1;
}

TEST_CASE("Sample", "[sample]") {
  REQUIRE(add_one(1) == 2);
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
