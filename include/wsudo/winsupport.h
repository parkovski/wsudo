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

template<typename H, auto Free>
class Handle;

namespace detail {
  template<typename T, auto Free>
  class pointer_operators {};

  template<auto Free>
  class pointer_operators<void *, Free> {};

  template<auto Free>
  class pointer_operators<const void *, Free> {};

  template<typename T, auto Free>
  class pointer_operators<T *, Free> {
    using Self = Handle<T *, Free>;

  public:
    T *operator->() {
      return static_cast<Self *>(this)->operator T *();
    }

    const T *operator->() const {
      return static_cast<const Self *>(this)->operator const T *();
    }

    T &operator*() {
      return *operator->();
    }

    const T &operator*() const {
      return *operator->();
    }
  };
} // namespace detail

// RAII wrapper around Windows HANDLE values. Free should be the function that
// closes this type of handle (e.g. CloseHandle).
// This class is basically a reimplementation of unique_ptr, except that it has
// some conveniences to make it easier to use in Windows API calls.
template<typename H, auto Free>
class Handle : public detail::pointer_operators<H, Free> {
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

  // Frees the old handle if any before assignment.
  Handle<H, Free> &operator=(Handle<H, Free> &&other) {
    free();
    _handle = other.take();
    return *this;
  }

  // Frees the old handle if any before assignment.
  Handle<H, Free> &operator=(H newHandle) {
    free();
    _handle = newHandle;
    return *this;
  }

  // Gets a pointer to the inner handle. Be careful not to leak memory when
  // overwriting the handle value.
  H *operator&() {
    return &_handle;
  }

  // Gets a const pointer to the inner handle.
  const H *operator&() const {
    return &_handle;
  }

  // Release the handle without freeing it.
  H take() {
    auto handle = _handle;
    _handle = nullptr;
    return handle;
  }

  // Implicit conversion to a Windows handle - makes it easier to pass to
  // Win API functions.
  operator H() {
    return _handle;
  }

  operator const H() const {
    return _handle;
  }

  // Returns true if the handle is not null - does not test the actual validity
  // of the handle.
  bool good() const {
    return !!_handle;
  }

  // Null test.
  explicit operator bool() const {
    return !!_handle;
  }
};

// Standard HANDLE with CloseHandle destructor.
using HObject = Handle<HANDLE, CloseHandle>;

// Pointer returned from Windows that needs to be freed with LocalFree.
template<typename P>
using HLocalPtr = Handle<P, LocalFree>;

} // namespace wsudo
