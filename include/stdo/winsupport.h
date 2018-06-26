#ifndef STDO_WINSUPPORT_H
#define STDO_WINSUPPORT_H

#include <Windows.h>
#include <system_error>
#include <string>
#include <string_view>
#include <optional>

namespace stdo {

std::string to_utf8(std::wstring_view utf16str);
std::wstring to_utf16(std::string_view utf8str);

bool setThreadName(const wchar_t *name);

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

// Wrapper for calling dynamically into modules already loaded in this process.
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

// RAII wrapper around Windows HANDLE values.
template<typename H, auto Free>
class Handle {
  H _handle;

  void free() {
    if (_handle) {
      Free(_handle);
    }
  }

public:
  Handle() noexcept : _handle() {}
  explicit Handle(H handle) noexcept : _handle(handle) {}
  Handle(Handle<H, Free> &&other) noexcept : _handle(other.take()) {}

  ~Handle() {
    free();
  }

  Handle<H, Free> &operator=(Handle<H, Free> &&other) {
    free();
    _handle = other.take();
    return *this;
  }

  Handle<H, Free> &operator=(H newHandle) {
    free();
    _handle = newHandle;
    return *this;
  }

  // Careful: Easy to leak resources with this.
  H &operator*() {
    return _handle;
  }

  const H &operator*() const {
    return _handle;
  }

  // Careful: Easy to leak resources with this.
  H *operator&() {
    return &_handle;
  }

  const H *operator&() const {
    return &_handle;
  }

  // Release the handle without freeing it.
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

using HObject = Handle<HANDLE, CloseHandle>;

} // namespace stdo

#endif // STDO_WINSUPPORT_H
