#include "wsudo/wsudo.h"

#include <codecvt>

namespace wsudo {

#pragma warning(push)
#pragma warning(disable: 4996) // codecvt deprecation warning

std::string to_utf8(std::wstring_view utf16str) {
  if (utf16str.empty()) {
    return std::string{};
  }

  return std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t>{}
          .to_bytes(&utf16str.front(), &utf16str.back() + 1);
}

std::wstring to_utf16(std::string_view utf8str) {
  if (utf8str.empty()) {
    return std::wstring{};
  }

  return std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t>{}
            .from_bytes(&utf8str.front(), &utf8str.back() + 1);
}

#pragma warning(pop)

bool setThreadName(const wchar_t *name) {
  try {
    return LinkedModule(L"kernel32.dll")
      .get<decltype(&SetThreadDescription)>("SetThreadDescription")(
        GetCurrentThread(), name
      ) == S_OK;
  } catch (module_load_error &) {
    return false;
  }
}

std::string lastErrorString(DWORD status) {
  constexpr DWORD bufferSize = 1024;
  char buffer[bufferSize];
  // MSDN: Need to specify IGNORE_INSERTS with FROM_SYSTEM to avoid potential
  // bad memory access.
  auto size = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM |
                               FORMAT_MESSAGE_IGNORE_INSERTS,
                             nullptr, status, 0, buffer, bufferSize, nullptr);
  // Get rid of the final new line.
  if (size && buffer[size - 1] == '\n') {
    --size;
    if (size && buffer[size - 1] == '\r') {
      --size;
    }
  }
  return std::string{buffer, buffer + size};
}

} // namespace wsudo

