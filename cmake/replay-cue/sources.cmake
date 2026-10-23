# Replay Cue source list, shared by the release build (CMakeLists.txt) and the
# local macOS development build (tools/macos-dev).

set(
  REPLAY_CUE_SOURCES
  src/capture-engine.cpp
  src/capture-output.cpp
  src/clip-writer.cpp
  src/cue-dock.cpp
  src/ffmpeg-compat.cpp
  src/highlights-tab.cpp
  src/packet-buffer.cpp
  src/plugin-main.cpp
  src/rc-common.cpp
  src/replay-config.cpp
  src/replay-controller.cpp
  src/replay-event.cpp
  src/replay-player.cpp
  src/settings-dialog.cpp
  src/sport.cpp
  src/time-stretch.cpp
  src/websocket-api.cpp
)

set(
  REPLAY_CUE_HEADERS
  src/capture-engine.hpp
  src/capture-output.hpp
  src/clip-writer.hpp
  src/cue-dock.hpp
  src/ffmpeg-compat.hpp
  src/highlights-tab.hpp
  src/packet-buffer.hpp
  src/plugin-support.h
  src/rc-common.hpp
  src/replay-config.hpp
  src/replay-controller.hpp
  src/replay-event.hpp
  src/replay-player.hpp
  src/settings-dialog.hpp
  src/sport.hpp
  src/time-stretch.hpp
  src/third-party/obs-websocket-api.h
  src/websocket-api.hpp
)
