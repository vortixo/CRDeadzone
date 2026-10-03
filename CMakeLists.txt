cmake_minimum_required(VERSION 3.20)

project(CRDeadzone VERSION 1.2.0 LANGUAGES C CXX)

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

# --- GameInput headers (Microsoft.GameInput NuGet) ---------------------------
# Needed for the version-exact COM wrapper. The package is immutable on
# nuget.org, so the pinned version stays fetchable. If the fetch fails, v2/v3
# wrapping compiles out and the HID/XInput layers still work.
set(CRDEADZONE_GAMEINPUT_NUGET_VERSION "3.5.283" CACHE STRING "Microsoft.GameInput NuGet version")
set(CRDEADZONE_GAMEINPUT_DIR "${CMAKE_BINARY_DIR}/_gameinput" CACHE PATH "GameInput package dir")
set(CRDEADZONE_GAMEINPUT_INCLUDE "")

if(NOT EXISTS "${CRDEADZONE_GAMEINPUT_DIR}/native/include/GameInput.h")
  message(STATUS "Fetching Microsoft.GameInput ${CRDEADZONE_GAMEINPUT_NUGET_VERSION} ...")
  file(DOWNLOAD
    "https://www.nuget.org/api/v2/package/Microsoft.GameInput/${CRDEADZONE_GAMEINPUT_NUGET_VERSION}"
    "${CRDEADZONE_GAMEINPUT_DIR}/pkg.nupkg"
    SHOW_PROGRESS STATUS CRDEADZONE_GI_DL_STATUS)
  list(GET CRDEADZONE_GI_DL_STATUS 0 CRDEADZONE_GI_DL_CODE)
  if(CRDEADZONE_GI_DL_CODE EQUAL 0)
    execute_process(
      COMMAND ${CMAKE_COMMAND} -E tar xf "${CRDEADZONE_GAMEINPUT_DIR}/pkg.nupkg"
      WORKING_DIRECTORY "${CRDEADZONE_GAMEINPUT_DIR}"
      RESULT_VARIABLE CRDEADZONE_GI_UNPACK)
    if(NOT CRDEADZONE_GI_UNPACK EQUAL 0)
      message(WARNING "GameInput package unpack failed; v2/v3 wrapping disabled")
    endif()
  else()
    message(WARNING "GameInput package download failed; v2/v3 wrapping disabled")
  endif()
endif()

if(EXISTS "${CRDEADZONE_GAMEINPUT_DIR}/native/include/GameInput.h")
  set(CRDEADZONE_GAMEINPUT_INCLUDE "${CRDEADZONE_GAMEINPUT_DIR}/native/include")
endif()

# Verify the exact constructs used by GameInputWrapperV3.cpp against the real
# headers. A failed probe disables that version (safe) instead of breaking
# the build.
set(CRDEADZONE_GAMEINPUT_WRAPPER_SRC "src/GameInputWrapperStub.cpp")
if(NOT CRDEADZONE_GAMEINPUT_INCLUDE STREQUAL "")
  include(CheckCXXSourceCompiles)
  set(CMAKE_REQUIRED_INCLUDES "${CRDEADZONE_GAMEINPUT_INCLUDE}")
  check_cxx_source_compiles("
    #include <GameInput.h>
    int main() {
      auto m = &GameInput::v3::IGameInputReading::GetGamepadState;
      (void)m;
      return 0;
    }" CRDEADZONE_PROBE_V3_BASIC)
  if(CRDEADZONE_PROBE_V3_BASIC)
    try_compile(CRDEADZONE_HAVE_GAMEINPUT_V3
      "${CMAKE_BINARY_DIR}/_giprobe3"
      "${CMAKE_SOURCE_DIR}/cmake/GameInputProbeV3.cpp"
      CMAKE_FLAGS "-DINCLUDE_DIRECTORIES=${CRDEADZONE_GAMEINPUT_INCLUDE}"
      OUTPUT_VARIABLE CRDEADZONE_PROBE3_LOG)
    if(NOT CRDEADZONE_HAVE_GAMEINPUT_V3)
      message(STATUS "GameInput v3 probe failed; v3 wrapping disabled\n${CRDEADZONE_PROBE3_LOG}")
    endif()
  else()
    message(STATUS "GameInput headers present but v3 namespace unusable; wrapping disabled")
  endif()
endif()

if(CRDEADZONE_HAVE_GAMEINPUT_V3 OR CRDEADZONE_HAVE_GAMEINPUT_V2)
  set(CRDEADZONE_GAMEINPUT_WRAPPER_SRC "src/GameInputWrapperV3.cpp")
endif()

add_library(CRDeadzone SHARED
  src/main.cpp
  src/PatternScanner.cpp
  src/Disasm.cpp
  src/XInputHook.cpp
  src/GameInputHook.cpp
  ${CRDEADZONE_GAMEINPUT_WRAPPER_SRC}
  src/RawInputHook.cpp
  src/HidReadHook.cpp
  src/HidReport.cpp
  src/HidMapping.cpp
  src/Activity.cpp
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

if(NOT CRDEADZONE_GAMEINPUT_INCLUDE STREQUAL "")
  target_include_directories(CRDeadzone PRIVATE "${CRDEADZONE_GAMEINPUT_INCLUDE}")
endif()
if(CRDEADZONE_HAVE_GAMEINPUT_V3)
  target_compile_definitions(CRDeadzone PRIVATE CRDEADZONE_HAVE_GAMEINPUT_V3=1)
endif()

target_compile_definitions(CRDeadzone PRIVATE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  _CRT_SECURE_NO_WARNINGS
)

# hid.lib (HidP_/HidD_ APIs) ships with the Windows SDK.
target_link_libraries(CRDeadzone PRIVATE hid)
set_target_properties(CRDeadzone PROPERTIES OUTPUT_NAME "CRDeadzone")

# Place the menu descriptor next to the DLL in the build tree for local testing.
add_custom_command(TARGET CRDeadzone POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/deadzone.menu.json"
    "$<TARGET_FILE_DIR:CRDeadzone>/deadzone.menu.json"
)
