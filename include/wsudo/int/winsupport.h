#if !defined(WSUDO_WSUDO_H)
#error "Do not include this file directly; use wsudo.h."
#endif

#include <exception>
#include <string>
#include <string_view>
#include <type_traits>

namespace wsudo {

std::string to_utf8(std::wstring_view utf16str);
std::wstring to_utf16(std::string_view utf8str);

// Set thread name via new Win10 API, if it exists.
// Returns false if the API was not found or failed.
bool setThreadName(const wchar_t *name);

// Convert a "GetLastError" code to string.
std::string lastErrorString(DWORD status);

// Convenience GetLastError->string.
inline std::string lastErrorString() {
  return lastErrorString(::GetLastError());
}

// Dynamic module (DLL) load error.
class module_load_error : public std::exception {
  DWORD _error;
  std::string _message;
public:
  explicit module_load_error(const char *module)
    : _error{GetLastError()},
      _message{
        std::string("Module '") + module + "' load failed: " +
          lastErrorString(_error)
      }
  {}

  explicit module_load_error(const char *module, const char *function)
    : _error{GetLastError()},
      _message{
        std::string("Function '") + module + "!" + function + "' not found: " +
          lastErrorString(_error)
      }
  {}

  DWORD syscode() const {
    return _error;
  }

  const char *what() const override {
    return _message.c_str();
  }
};

// Wrapper for calling dynamically into modules already loaded in this process.
// Used for ntdll which must be called dynamically.
class LinkedModule {
  const wchar_t *_name;
  HMODULE _module;

public:
  // Throws a module_load_error on failure.
  LinkedModule(const wchar_t *dllName)
    : _name{dllName},
      _module{GetModuleHandleW(dllName)}
  {
    if (!_module) {
      throw module_load_error{to_utf8(_name).c_str()};
    }
  }

  template<typename, typename> class Function;
  // Note: F may have a calling convention; R(Args...) does not.
  template<typename F, typename R, typename... Args>
  class Function<F, R(*)(Args...)> {
  public:
    // Throws a module_load_error on failure.
    Function(const LinkedModule &module, const char *name)
      : _function(reinterpret_cast<F>(GetProcAddress(module._module, name)))
    {
      if (!_function) {
        throw module_load_error{to_utf8(module._name).c_str(), name};
      }
    }

    R operator()(Args ...args) const {
      return _function(std::forward<Args>(args)...);
    }

  private:
    F _function;
  };

  // Get a pointer to the function `name` with type F from this module.
  // Throws a module_load_error on failure.
  template<typename F>
  Function<F, F> get(const char *name) const {
    return Function<F, F>{*this, name};
  }
};

} // namespace wsudo
