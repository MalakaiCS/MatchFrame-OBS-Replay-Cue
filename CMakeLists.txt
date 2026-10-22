cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

if(NOT ENABLE_FRONTEND_API OR NOT ENABLE_QT)
  message(FATAL_ERROR "Replay Cue needs ENABLE_FRONTEND_API and ENABLE_QT (its cue panel is a Qt dock).")
endif()

include(compilerconfig)
include(defaults)
include(helpers)
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/replay-cue/sources.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/replay-cue/ffmpeg.cmake")

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

find_package(obs-frontend-api REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)

find_package(Qt6 COMPONENTS Widgets Core REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
target_compile_options(
  ${CMAKE_PROJECT_NAME}
  PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
)
# No moc_predefs.h: generating it runs clang++ inside an Xcode 26 script phase,
# where conflicting *_DEPLOYMENT_TARGET variables make the compiler refuse to run.
set_target_properties(
  ${CMAKE_PROJECT_NAME}
  PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON AUTOMOC_COMPILER_PREDEFINES OFF
)

replay_cue_link_ffmpeg(${CMAKE_PROJECT_NAME})

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${REPLAY_CUE_SOURCES} ${REPLAY_CUE_HEADERS})

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
