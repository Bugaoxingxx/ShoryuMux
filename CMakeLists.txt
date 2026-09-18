cmake_minimum_required(VERSION 3.16)
project(shoryumuxd C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)

set(CORE_COMMON
  core/main.c
  core/config.c
  core/actions.c
  core/xinput_report.c
)

if(WIN32)
  add_executable(shoryumuxd
    ${CORE_COMMON}
    core/compat_win.c
    core/input_win.c
    core/output_win.c
  )
  target_link_libraries(shoryumuxd PRIVATE Xinput user32)
  if(MSVC)
    target_compile_definitions(shoryumuxd PRIVATE _CRT_SECURE_NO_WARNINGS)
  endif()
elseif(APPLE)
  add_executable(shoryumuxd
    ${CORE_COMMON}
    core/compat_posix.c
    core/input_macos.c
    core/output_macos.c
  )
  target_link_libraries(shoryumuxd PRIVATE
    "-framework IOKit"
    "-framework CoreFoundation"
    "-framework ApplicationServices"
    "-framework CoreGraphics"
  )
else()
  add_executable(shoryumuxd
    ${CORE_COMMON}
    core/compat_posix.c
    core/input_linux.c
    core/output_linux.c
  )
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(LIBUSB REQUIRED libusb-1.0)
  target_include_directories(shoryumuxd PRIVATE ${LIBUSB_INCLUDE_DIRS})
  target_link_libraries(shoryumuxd PRIVATE ${LIBUSB_LIBRARIES})
endif()

target_include_directories(shoryumuxd PRIVATE core)
