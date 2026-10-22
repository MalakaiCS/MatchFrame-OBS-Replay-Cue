# Local macOS development build.
#
# The release build (../../CMakeLists.txt) follows the official OBS plugin
# template, which compiles libobs from source and needs full Xcode. This project
# instead links straight against an installed OBS.app, so it builds with only the
# Command Line Tools. Use build.sh rather than calling CMake directly.

cmake_minimum_required(VERSION 3.28)

set(REPLAY_CUE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
file(READ "${REPLAY_CUE_ROOT}/buildspec.json" _buildspec)
string(JSON _name GET ${_buildspec} name)
string(JSON _version GET ${_buildspec} version)

project(${_name} VERSION ${_version} LANGUAGES C CXX)

set(OBS_APP "" CACHE PATH "OBS.app to link against and test with")
set(OBS_SOURCE "" CACHE PATH "obs-studio source tree of the same version as OBS_APP")
set(OBS_DEPS "" CACHE PATH "obs-deps macOS package of the same version as OBS_APP")
foreach(var IN ITEMS OBS_APP OBS_SOURCE OBS_DEPS)
  if(NOT EXISTS "${${var}}")
    message(FATAL_ERROR "Set ${var} (see tools/macos-dev/build.sh)")
  endif()
endforeach()

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_OSX_DEPLOYMENT_TARGET 13.0)
set(CMAKE_AUTOMOC ON)
# Matches the release build, which cannot generate moc_predefs.h under Xcode 26.
set(CMAKE_AUTOMOC_COMPILER_PREDEFINES OFF)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

include("${REPLAY_CUE_ROOT}/cmake/replay-cue/sources.cmake")
list(TRANSFORM REPLAY_CUE_SOURCES PREPEND "${REPLAY_CUE_ROOT}/")
list(TRANSFORM REPLAY_CUE_HEADERS PREPEND "${REPLAY_CUE_ROOT}/")

configure_file("${REPLAY_CUE_ROOT}/src/plugin-support.c.in" "${CMAKE_BINARY_DIR}/plugin-support.c" @ONLY)
file(
  WRITE
  "${CMAKE_BINARY_DIR}/generated/obsconfig.h"
  "#pragma once\n#define OBS_RELEASE_CANDIDATE 0\n#define OBS_BETA 0\n"
)

add_library(${PROJECT_NAME} MODULE ${REPLAY_CUE_SOURCES} ${REPLAY_CUE_HEADERS} "${CMAKE_BINARY_DIR}/plugin-support.c")

set_target_properties(
  ${PROJECT_NAME}
  PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION plugin
    PREFIX ""
    MACOSX_BUNDLE_BUNDLE_NAME ${PROJECT_NAME}
    MACOSX_BUNDLE_GUI_IDENTIFIER io.github.malakaics.obs-replay-cue
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "@executable_path/../Frameworks"
)

target_include_directories(
  ${PROJECT_NAME}
  PRIVATE "${REPLAY_CUE_ROOT}/src" "${CMAKE_BINARY_DIR}/generated" "${OBS_SOURCE}/libobs" "${OBS_SOURCE}/frontend/api"
)
target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE "${OBS_DEPS}/include")

target_compile_options(
  ${PROJECT_NAME}
  PRIVATE
    -Wall
    -Wextra
    -Wno-unused-parameter
    -Wno-missing-field-initializers
    -Wshorten-64-to-32
    -Wno-quoted-include-in-framework-header
)

target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE
    Qt6::Core
    Qt6::Widgets
    "${OBS_APP}/Contents/Frameworks/obs-frontend-api.dylib"
    "${OBS_DEPS}/lib/libavcodec.dylib"
    "${OBS_DEPS}/lib/libavformat.dylib"
    "${OBS_DEPS}/lib/libavutil.dylib"
    "${OBS_DEPS}/lib/libswscale.dylib"
)

# Passed as a raw linker input: listing it as a library would add OBS.app's
# Frameworks folder to the framework search path, where Qt has no headers.
target_link_options(${PROJECT_NAME} PRIVATE "${OBS_APP}/Contents/Frameworks/libobs.framework/Versions/A/libobs")

add_custom_command(
  TARGET ${PROJECT_NAME}
  POST_BUILD
  COMMAND
    "${CMAKE_COMMAND}" -E copy_directory "${REPLAY_CUE_ROOT}/data"
    "$<TARGET_BUNDLE_CONTENT_DIR:${PROJECT_NAME}>/Resources"
  COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:${PROJECT_NAME}>"
  VERBATIM
)
