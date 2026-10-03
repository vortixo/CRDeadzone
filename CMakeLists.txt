cmake_minimum_required(VERSION 3.20)

project(CRDeadzone VERSION 1.0.0 LANGUAGES C CXX)

# The game and all hooks are 64-bit Windows only.
if(NOT WIN32)
  message(FATAL_ERROR "CRDeadzone targets Windows x64 only. Configure on Windows or use GitHub Actions.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static CRT (/MT) so players need no extra redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(CRDEADZONE_MINHOOK_LOCAL "Use a local MinHook checkout instead of FetchContent" OFF)
set(CRDEADZONE_MINHOOK_SOURCE_DIR "" CACHE PATH "Path to a local MinHook checkout (used when CRDEADZONE_MINHOOK_LOCAL=ON)")

include(FetchContent)

if(CRDEADZONE_MINHOOK_LOCAL)
  if(CRDEADZONE_MINHOOK_SOURCE_DIR STREQUAL "")
    message(FATAL_ERROR "CRDEADZONE_MINHOOK_LOCAL=ON requires CRDEADZONE_MINHOOK_SOURCE_DIR")
  endif()
  set(minhook_SOURCE_DIR "${CRDEADZONE_MINHOOK_SOURCE_DIR}")
else()
  FetchContent_Declare(
    MinHook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG        v1.3.3
  )
  FetchContent_MakeAvailable(MinHook)
endif()

# MinHook upstream has no stable CMake target across revisions, so compile its
# sources directly. This is robust to upstream layout changes.
file(GLOB MINHOOK_SOURCES
  "${minhook_SOURCE_DIR}/src/*.c"
  "${minhook_SOURCE_DIR}/src/hde/*.c"
)
if(MINHOOK_SOURCES STREQUAL "")
  message(FATAL_ERROR "No MinHook sources found under ${minhook_SOURCE_DIR}/src")
endif()

add_library(CRDeadzone SHARED
  src/main.cpp
  src/PatternScanner.cpp
  src/XInputHook.cpp
  src/GameInputHook.cpp
  src/OptionsOverride.cpp
  src/Config.cpp
  src/DeadzoneMath.cpp
  src/Logger.cpp
  ${MINHOOK_SOURCES}
)

target_include_directories(CRDeadzone PRIVATE
  include
  "${minhook_SOURCE_DIR}/include"
  "${minhook_SOURCE_DIR}/src"
)

target_compile_definitions(CRDeadzone PRIVATE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  _CRT_SECURE_NO_WARNINGS
)

# Official GameInput SDK header (ships with recent Windows SDKs). When present
# we compile the GameInput vtable hook; otherwise we build XInput + Options
# paths only and log that GameInput support was compiled out.
include(CheckIncludeFileCXX)
check_include_file_cxx("GameInput.h" HAVE_GAMEINPUT_H)
if(HAVE_GAMEINPUT_H)
  target_compile_definitions(CRDeadzone PRIVATE CRDEADZONE_HAVE_GAMEINPUT=1)
else()
  message(STATUS "GameInput.h not found - GameInput hook will be compiled out (XInput + Options paths remain)")
endif()

set_target_properties(CRDeadzone PROPERTIES OUTPUT_NAME "CRDeadzone")

# Place the menu descriptor next to the DLL in the build tree for local testing.
add_custom_command(TARGET CRDeadzone POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/deadzone.menu.json"
    "$<TARGET_FILE_DIR:CRDeadzone>/deadzone.menu.json"
)
