#include "wsudo/wsudo.h"

namespace wsudo {

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

bool setThreadName(const wchar_t *name, HANDLE thread) {
  if (thread == INVALID_HANDLE_VALUE) {
    thread = GetCurrentThread();
  }
  try {
    return LinkedModule(L"kernel32.dll")
      .get<decltype(&SetThreadDescription)>("SetThreadDescription")(
        thread, name
      ) == S_OK;
  } catch (module_load_error &) {
    return false;
  }
}

std::wstring getThreadName(HANDLE thread) {
  if (thread == INVALID_HANDLE_VALUE) {
    thread = GetCurrentThread();
  }
  try {
    PWSTR name;
    if (
      LinkedModule(L"kernel32.dll")
        .get<decltype(&GetThreadDescription)>("GetThreadDescription")(
          thread, &name
        ) == S_OK
    ) {
      std::wstring wstrName{name};
      LocalFree(name);
      return wstrName;
    }
  } catch (module_load_error &) {
  }
  return {};
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

bool resolveProgramPath(std::wstring &program) {
  if (program.empty()) {
    return false;
  }
  size_t i, j;
  std::wstring resolvedPath;
  bool searchPath = false;
  DWORD curdirLength = GetCurrentDirectory(0, nullptr);
  auto curdir = std::make_unique<wchar_t[]>(curdirLength);
  GetCurrentDirectory(curdirLength, curdir.get());
  auto issep = [](wchar_t ch) noexcept { return ch == '\\' || ch == '/'; };
  if (issep(program[0])) {
    if (program.length() > 1 && issep(program[1])) {
      if (
        program.length() > 3
        && program[0] == '\\' && program[1] == '\\'
        && program[2] == '?' && program[3] == '\\'
      ) {
        // Absolute NT path (\\?\...) - must use backslashes.
        resolvedPath = program;
      } else {
        // UNC path.
        resolvedPath = program;
      }
    } else {
      // Absolute path without drive letter.
      if (issep(curdir[0]) && issep(curdir[1])) {
        // curdir is a UNC path. The root is in the form "\\server\share".
        for (i = 2; curdir[i] != 0; ++i) {
          if (issep(curdir[i])) {
            // Server is curdir[2..i].
            for (j = i + 1; curdir[j] != 0; ++j) {
              if (issep(curdir[j])) {
                // Share is curdir[i+1..j].
                // Root path is curdir[0..j].
                resolvedPath = std::wstring_view{curdir.get(), j};
                goto breakUnc;
              }
            }
            // Didn't find a \ or /. curdir is something like "\\server\share".
            resolvedPath = std::wstring_view{curdir.get(), j};
            goto breakUnc;
          }
        }
        // Didn't find something of the form "\\server\share...".
        log::error(L"Invalid UNC path \"{}\".", curdir.get());
        return false;
      breakUnc:
        // resolvedPath has no trailing \. program has an initial \.
        resolvedPath.append(program);
      } else {
        assert((curdir[0] | 0x20) >= L'a' && (curdir[0] | 0x20) <= L'z'
               && curdir[1] == L':');
        resolvedPath = std::wstring_view{curdir.get(), 2}; // e.g., C:
        resolvedPath += program; // program has an initial \.
      }
    }
  } else if (
    program.length() > 1
    && ((program[0] | 0x20) >= L'a' && (program[0] | 0x20) <= L'z')
    && program[1] == ':'
  ) {
    if (program.length() > 2 && issep(program[2])) {
      // Absolute path with drive letter.
      resolvedPath = program;
    } else {
      // Relative path with drive letter. Windows doesn't keep track of current
      // directories for each drive, so require that the drive is the same as
      // the one from the current directory.
      if ((program[0] | 0x20) != (curdir[0] | 0x20)) {
        log::error("Drive letter must match current directory.");
        return false;
      }
      // Now it's either a relative path to the current directory
      // (includes a \) or a path lookup (no \).
      for (i = 3; i < program.length(); ++i) {
        if (issep(program[i])) {
          resolvedPath = curdir.get();
          if (!resolvedPath.ends_with('\\')
            && !resolvedPath.ends_with('/')) {
            resolvedPath.push_back('\\');
          }
          resolvedPath.append(std::wstring_view{program}.substr(2));
          break;
        }
      }
      if (resolvedPath.empty()) {
        // Didn't find a \. It's path relative.
        resolvedPath = std::wstring_view{program}.substr(2);
        searchPath = true;
      }
    }
  } else if (program.length() > 1 && program[0] == '.' && issep(program[1])) {
    // Relative path to current dir.
    resolvedPath = curdir.get();
    if (!resolvedPath.ends_with('\\') && !resolvedPath.ends_with('/')) {
      resolvedPath.push_back('\\');
    }
    resolvedPath.append(std::wstring_view{program}.substr(2));
  } else {
    for (i = 0; i < program.length(); ++i) {
      if (issep(program[i])) {
        // Relative path to current dir.
        resolvedPath = curdir.get();
        if (!resolvedPath.ends_with('\\') && !resolvedPath.ends_with('/')) {
          resolvedPath.push_back('\\');
        }
        resolvedPath.append(program);
        break;
      }
    }
    if (resolvedPath.empty()) {
      // Relative to Path environment variable.
      resolvedPath = program;
      searchPath = true;
    }
  }

  auto isValidFile = [](std::wstring &path) -> bool {
    DWORD attributes = GetFileAttributes(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
      path += L".exe";
      attributes = GetFileAttributes(path.c_str());
      if (attributes == INVALID_FILE_ATTRIBUTES) {
        path.erase(path.length() - 4);
        return false;
      }
    }
    return true;
  };

  if (searchPath) {
    log::debug(L"Searching Path for \"{}\".", program);
    DWORD pathLength = GetEnvironmentVariable(L"Path", nullptr, 0);
    auto path = std::make_unique<wchar_t[]>(pathLength + 1);
    std::wstring potentialPath;
    GetEnvironmentVariable(L"Path", path.get(), pathLength + 1);
    i = j = 0;
    while (j <= pathLength) {
      if (path[j] == ';' || path[j] == '\0') {
        if (j == i) {
          // Empty path segment
          i = ++j;
          continue;
        }
        std::wstring_view segment{path.get() + i, j - i};
        potentialPath = segment;
        if (!segment.ends_with('\\') && !segment.ends_with('/')) {
          potentialPath.push_back('\\');
        }
        potentialPath.append(resolvedPath);
        if (isValidFile(potentialPath)) {
          program = potentialPath;
          log::debug(L"Found \"{}\".", program);
          return true;
        }
        i = ++j;
        continue;
      }
      ++j;
    }
  } else {
    if (isValidFile(resolvedPath)) {
      program = resolvedPath;
      log::debug(L"Found \"{}\".", program);
      return true;
    }
  }

  log::error(L"Could not find \"{}\".", program);
  log::trace(L"Note: path was resolved to \"{}\".", resolvedPath);
  return false;
}

std::wstring escapeCommandLineArg(std::wstring_view arg) {
  std::wstring newarg;
  bool hasSpace = false;
  newarg.reserve(arg.size());

  for (size_t i = 0; i < arg.size(); ++i) {
    switch (arg[i]) {
    case L' ':
    case L'\t':
    case L'\r':
    case L'\n':
      hasSpace = true;
      break;
    }
  }

  if (hasSpace) {
    newarg.push_back(L'"');
  }
  for (size_t i = 0; i < arg.size(); ++i) {
    switch (arg[i]) {
    case L'"':
      if (hasSpace) {
        newarg.append(L"\\\"");
      } else {
        newarg.append(L"\"\\\"");
        while (++i < arg.size()) {
          switch (arg[i]) {
          case L'"':
            newarg.append(L"\\\"");
            break;

          case L'\\':
            newarg.append(L"\\\\");
            break;

          default:
            newarg.push_back(arg[i]);
            break;
          }
        }
        --i;
        newarg.push_back(L'"');
      }
      break;

    case L'\\':
      {
        size_t j = i + 1;
        while (j < arg.size() && arg[j] == L'\\') {
          ++j;
        }
        if (j < arg.size() && arg[j] == '"') {
          if (!hasSpace) {
            newarg.push_back(L'"');
          }
          for (size_t k = i; k < j; ++k) {
            newarg.append(L"\\\\");
          }
          while (j < arg.size()) {
            switch (arg[j]) {
            case L'"':
              newarg.append(L"\\\"");
              break;

            case L'\\':
              newarg.append(L"\\\\");
              break;

            default:
              newarg.push_back(arg[j]);
              break;
            }
            ++j;
          }
          if (!hasSpace) {
            newarg.push_back(L'"');
          }
        } else {
          for (size_t k = i; k < j; ++k) {
            newarg.push_back(L'\\');
          }
        }
        i = j - 1;
      }
      break;

    default:
      newarg.push_back(arg[i]);
      break;
    }
  }
  if (hasSpace) {
    newarg.push_back('"');
  }

  return newarg;
}

} // namespace wsudo

