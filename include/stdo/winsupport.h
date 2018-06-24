#ifndef STDO_WINSUPPORT_H
#define STDO_WINSUPPORT_H

#include <Windows.h>
#include <system_error>
#include <string>
#include <string_view>
#include <codecvt>
#include <optional>

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

class module_load_error : public std::system_error {
public:
  explicit module_load_error(const char *module)
    : std::system_error{
      (int)GetLastError(), std::system_category(),
      std::string("Loading ") + module + "failed"
    }
  {}

  explicit module_load_error(const char *module, const char *function)
    : std::system_error{
      (int)GetLastError(), std::system_category(),
      std::string("Function ") + module + "!" + function + " not found"
    }
  {}
};

class LinkedModule {
  std::string _name;
  HMODULE _module;

public:
  LinkedModule(const wchar_t *dllName)
    : _name{to_utf8(dllName)},
      _module{GetModuleHandleW(dllName)}
  {
    if (!_module) {
      throw module_load_error{_name.c_str()};
    }
  }

  template<typename, typename> class Function;
  // Note: F may have a calling convention; R(Args...) does not.
  template<typename F, typename R, typename... Args>
  class Function<F, R(*)(Args...)> {
    F _function;
  public:
    Function(const LinkedModule &module, const char *name)
      : _function(reinterpret_cast<F>(GetProcAddress(module._module, name)))
    {
      if (!_function) {
        throw module_load_error{module._name.c_str(), name};
      }
    }

    R operator()(Args ...args) const {
      return _function(std::forward<Args>(args)...);
    }
  };

  template<typename F>
  Function<F, F> get(const char *name) const {
    return Function<F, F>{*this, name};
  }
};

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

template<typename H, auto Free>
class Handle {
  H _handle;

public:
  Handle() noexcept : _handle() {}
  explicit Handle(H handle) noexcept : _handle(handle) {}
  Handle(Handle<H, Free> &&other) noexcept : _handle(other.take()) {}

  ~Handle() {
    if (_handle) {
      Free(_handle);
    }
  }

  Handle<H, Free> &operator=(H handle) {
    return replace(handle);
  }

  Handle<H, Free> &operator=(Handle<H, Free> &&other) {
    return replace(other.take());
  }

  Handle<H, Free> &replace(H newHandle = nullptr) {
    if (_handle) {
      Free(_handle);
    }
    _handle = newHandle;
    return *this;
  }

  /// Careful: Easy to leak resources with this.
  H &operator*() {
    return _handle;
  }

  const H &operator*() const {
    return _handle;
  }

  /// Careful: Easy to leak resources with this.
  H *operator&() {
    return &_handle;
  }

  const H *operator&() const {
    return &_handle;
  }

  /// Release the handle without freeing it.
  H take() {
    auto handle = _handle;
    _handle = nullptr;
    return handle;
  }

  operator H() const {
    return _handle;
  }

  bool good() const {
    return !!_handle;
  }

  explicit operator bool() const {
    return !!_handle;
  }
};

using HStdHandle = Handle<HANDLE, CloseHandle>;

#endif // STDO_WINSUPPORT_H
