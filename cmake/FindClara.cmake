find_path(Clara_INCLUDE_DIRS clara.hpp)

if(Clara_INCLUDE_DIRS)
  add_library(Clara INTERFACE IMPORTED)
  target_include_directories(Clara INTERFACE ${Clara_INCLUDE_DIRS})
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Clara REQUIRED_VARS Clara_INCLUDE_DIRS)
