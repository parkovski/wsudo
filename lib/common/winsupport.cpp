#include "wsudo/wsudo.h"

namespace wsudo {

#pragma warning(push)
#pragma warning(disable: 4996) // codecvt deprecation warning

std::string to_utf8(std::wstring_view utf16str) {
  if (utf16str.empty()) {
    return std::string{};
  }

  int size = WideCharToMultiByte(CP_UTF8, 0, utf16str.data(), utf16str.size(),
                                 nullptr, 0, nullptr, nullptr);

  std::string utf8str;
  utf8str.resize(size);
  WideCharToMultiByte(CP_UTF8, 0, utf16str.data(), utf16str.size(),
                      utf8str.data(), size, nullptr, nullptr);

  return utf8str;
}

std::wstring to_utf16(std::string_view utf8str) {
  if (utf8str.empty()) {
    return std::wstring{};
  }

  int size = MultiByteToWideChar(CP_UTF8, 0, utf8str.data(), utf8str.size(),
                                 nullptr, 0);

  std::wstring utf16str;
  utf16str.resize(size);
  MultiByteToWideChar(CP_UTF8, 0, utf8str.data(), utf8str.size(),
                      utf16str.data(), size);

  return utf16str;
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

std::wstring lastErrorWString(DWORD status) {
  constexpr DWORD bufferSize = 1024;
  wchar_t buffer[bufferSize];
  // MSDN: Need to specify IGNORE_INSERTS with FROM_SYSTEM to avoid potential
  // bad memory access.
  auto size = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM |
                               FORMAT_MESSAGE_IGNORE_INSERTS,
                             nullptr, status, 0, buffer, bufferSize, nullptr);
  // Get rid of the final new line.
  if (size && buffer[size - 1] == L'\n') {
    --size;
    if (size && buffer[size - 1] == L'\r') {
      --size;
    }
  }
  return std::wstring{buffer, buffer + size};
}

} // namespace wsudo

