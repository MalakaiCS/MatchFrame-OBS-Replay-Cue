# Changelog

All notable changes to Replay Cue are listed here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and versions follow
[Semantic Versioning](https://semver.org/). The section for a tagged version
becomes the notes of its GitHub release.

## [1.0.0] - 2026-10-22

First release of Replay Cue: multi-angle instant replay inside OBS Studio.

### Added

- Per-camera replay buffers for up to 8 angles. Each angle is rendered
  off-screen, encoded with its own (preferably hardware) encoder session and
  kept in memory for the last N seconds.
- Frame-accurate multi-angle sync on OBS's video clock: switching angles
  mid-replay changes the picture on the same frame.
- Mark IN / Mark OUT and one-press Instant Replay (last N seconds), with
  optional operator-delay compensation.
- Replay cue list with name, tag, favorite, length and angle count.
  Double-click to play, multi-select to play a sequence.
- Replay Cue Player source on a dedicated Replay scene, with your own
  transitions in and out, automatic return to live, auto play next and cycle
  angles.
- Slow motion (25/50/75% presets or any speed from 5–200%), frame stepping,
  scrubbing, pause, and Cue (load paused, into Preview in Studio Mode).
- Folder import: clips in the replay folder become replays, multi-angle files
  are grouped by camera, and OBS Replay Buffer saves are picked up.
- Export one angle or all angles, trimmed IN→OUT or full, as MP4 stream copies.
- 31 OBS hotkeys and 37 obs-websocket vendor requests (`ReplayCue`) plus
  vendor events, for Stream Deck, Bitfocus Companion, Touch Portal or scripts.
- `tools/replaycue_ws.py`, a dependency-free obs-websocket command-line client.
- Packages for Windows x64 (`.zip`), macOS universal (`.pkg`) and
  Ubuntu 24.04 x86_64 (`.deb`).

### Requirements

- OBS Studio 32.2 or newer within the 32.x series. The plugin checks the
  FFmpeg libraries bundled with OBS at load time and disables recording and
  playback with a clear message if they do not match.
- Windows 10/11 x64, macOS 13+ (Apple Silicon or Intel), or Ubuntu 24.04+.

### Known limitations

- Exports start on the keyframe at or before IN (stream copy, no re-encode).
- HDR canvases are captured as SDR (Rec. 709).
- Replays play audio only at 100% speed; slow motion is silent.

[1.0.0]: https://github.com/MalakaiCS/MatchFrame-OBS-Replay-Cue/releases/tag/1.0.0
