# Links the FFmpeg libraries that ship with OBS Studio.
#
# Replay Cue must be built against the same FFmpeg major version as the OBS
# release it runs in. On macOS and Windows that is the obs-deps package named in
# buildspec.json; on Linux it is the distribution's FFmpeg, which the OBS PPA
# also uses. The plugin double-checks the versions at load time.

include_guard(GLOBAL)

function(replay_cue_link_ffmpeg target)
  if(OS_LINUX OR OS_FREEBSD OR OS_OPENBSD)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(REPLAY_CUE_FFMPEG REQUIRED IMPORTED_TARGET libavcodec libavformat libavutil libswscale)
    target_link_libraries(${target} PRIVATE PkgConfig::REPLAY_CUE_FFMPEG)
    return()
  endif()

  find_path(REPLAY_CUE_FFMPEG_INCLUDE_DIR libavcodec/avcodec.h REQUIRED)
  target_include_directories(${target} SYSTEM PRIVATE "${REPLAY_CUE_FFMPEG_INCLUDE_DIR}")

  foreach(component IN ITEMS avcodec avformat avutil swscale)
    find_library(REPLAY_CUE_FFMPEG_${component}_LIBRARY NAMES ${component} lib${component} REQUIRED)
    target_link_libraries(${target} PRIVATE "${REPLAY_CUE_FFMPEG_${component}_LIBRARY}")
  endforeach()
endfunction()
