find_path(sodium_INCLUDE_DIR sodium.h)
find_library(sodium_LIBRARY NAMES libsodium sodium)

if(sodium_INCLUDE_DIR AND sodium_LIBRARY AND EXISTS ${sodium_INCLUDE_DIR}/sodium/core.h)
  add_library(sodium INTERFACE IMPORTED)
  target_link_libraries(sodium INTERFACE ${sodium_LIBRARY})
  target_include_directories(sodium INTERFACE ${sodium_INCLUDE_DIR})
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(sodium REQUIRED_VARS sodium_INCLUDE_DIR sodium_LIBRARY HANDLE_COMPONENTS)
