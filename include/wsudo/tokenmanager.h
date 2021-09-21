#pragma once

#include "wsudo.h"

#include <string_view>

namespace wsudo {

class TokenManager {
  DWORD _clientProcessId;
  HANDLE _token = nullptr;

public:
  // Creates a TokenManager for the identity that owns the client process.
  TokenManager(DWORD clientProcessId);

  ~TokenManager();

  // Returns true if the identity of this TokenManager is allowed to log on
  // with the given domain/username, and the password is correct.
  bool logon(std::wstring_view domain, std::wstring_view username,
             std::wstring_view password);

  // Duplicates the server's token.
  bool createServerToken();

  // Duplicates the remote process' token and changes the mandatory level.
  bool createRemoteToken(int level);

  // Creates a system token. Only available if the server is running as the
  // system account.
  bool createSystemToken();

  // Set the remote process's token to the currently stored token.
  bool applyToken(HANDLE remoteProcess);
};

} // namespace wsudo
