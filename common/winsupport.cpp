#include "stdo/winsupport.h"

#include <codecvt>

namespace stdo {

#pragma warning(push)
#pragma warning(disable: 4996) // codecvt deprecation warning

std::string to_utf8(std::wstring_view utf16str) {
  return std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t>{}
          .to_bytes(&utf16str.front(), &utf16str.back() + 1);
}

std::wstring to_utf16(std::string_view utf8str) {
  return std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t>{}
            .from_bytes(&utf8str.front(), &utf8str.back() + 1);
}

#pragma warning(pop)

bool setThreadName(const wchar_t *name) {
  try {
    return
      LinkedModule(L"kernel32.dll")
        .get<decltype(&SetThreadDescription)>("SetThreadDescription")
        (GetCurrentThread(), name) == S_OK;
  } catch (module_load_error &) {
    return false;
  }
}

std::string getSystemStatusString(DWORD status) {
  std::string msg;
  constexpr int bufferSize = 256;
  msg.resize(bufferSize);
  auto size = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, status, 0,
                             msg.data(), bufferSize, nullptr);
  msg.resize(size - 1); // Forget the NUL.
  return msg;
}

} // namespace stdo

