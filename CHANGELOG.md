# Changelog

All notable changes to Replay Cue are listed here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and versions follow
[Semantic Versioning](https://semver.org/). The section for a tagged version
becomes the notes of its GitHub release.

## [1.2.0] - 2026-10-23

### Added

- Export reel: the Highlights tab saves a highlights reel as one MP4 in the
  export folder (e.g. `…_Half-Time-highlights.mp4`), ready for social media.
  The ticked replays are joined from the selected angle without re-encoding,
  so it is quick and light on the computer during a broadcast. A clip that
  cannot be joined (a different camera size or codec) is left out and named.
  Also the `ExportHighlights` websocket request and `HighlightsExported` event.
- Automatic highlights: optionally put a break's reel on air by itself when the
  break is called (Settings → Sport, with a delay, 5 s by default). A replay
  already on air finishes first and the reel follows straight on; Back to Live
  cancels a reel that has not started. `SetOptions` takes `autoHighlights`.
- Slow-motion audio: replays now have sound at every speed. By default it is
  time-stretched so voices and crowd noise keep their pitch; it can instead be
  slowed down like tape (lower pitch) or kept silent (Settings → Playback).
- HDR capture: on an HDR canvas (Rec. 2100 PQ or HLG) cameras are recorded in
  10-bit HDR with an HEVC or AV1 encoder, and play back in HDR. *Automatic*
  picks an HEVC/AV1 hardware encoder for an HDR canvas; with an H.264 encoder,
  or one that refuses 10-bit, the camera is recorded in SDR as before. The dock
  shows which cameras record HDR.

### Changed

- Recorded and exported files are tagged with their color space (BT.709, or
  BT.2020 PQ/HLG for HDR), so other players and social sites show them
  correctly.
- The "Play audio at 100% speed" setting is now "Play replay audio".

### Fixed

- Two exports started together (two quick presses, or a reel and a replay)
  could pick the same file name, and the second overwrote the first. Exports
  now take the next free name (`_2`, `_3`…) when they are written.

## [1.1.0] - 2026-10-22

### Added

- Sports: pick football (soccer), rugby union, rugby league, Australian rules
  (AFL), Gaelic football / hurling, netball, hockey, basketball, American
  football or ice hockey in Settings → Sport. The sport sets the match
  structure (halves, quarters or periods), the names of its breaks and its
  quick tags.
- Match bar in the dock to move the match on (Start 1st Half → Half Time →
  Start 2nd Half → Full Time). Replays are stamped with the period they were
  saved in; a replay saved during a break belongs to the period that just
  ended. Replays can be moved to another period from their right-click menu.
- Highlights tab with a reel for every break: Half Time covers the first half,
  Full Time the whole match, and other breaks the period that just finished.
  Replays join the reel automatically and can be ticked in or out. Play
  highlights plays the reel oldest first, ⏮/⏭ step through it, and Loop keeps
  it going through the break until Back to Live.
- Optional highlights scene (for a HALF TIME HIGHLIGHTS graphic) and
  highlights speed.
- New match starts the next game with its own empty reels; the match state
  survives an OBS restart.
- Hotkeys: Next match period / break, Play highlights, Add / remove selected
  replay in highlights. The **H** key does the same in the dock.
- obs-websocket requests `GetMatch`, `NextMatchPhase`, `SetMatchPhase`,
  `NewMatch`, `PlayHighlights`, `SetHighlight` and `SetEventPeriod`, and the
  vendor events `MatchPhaseChanged` and `HighlightsStarted`. `GetStatus`
  reports the match phase, and replays carry `highlight`, `period` and `match`.

### Fixed

- Starting a replay while a Back to Live transition was still running could
  stop the new replay as soon as the transition finished, leaving the replay
  scene empty on air.

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

[1.2.0]: https://github.com/MalakaiCS/MatchFrame-OBS-Replay-Cue/releases/tag/1.2.0
[1.1.0]: https://github.com/MalakaiCS/MatchFrame-OBS-Replay-Cue/releases/tag/1.1.0
[1.0.0]: https://github.com/MalakaiCS/MatchFrame-OBS-Replay-Cue/releases/tag/1.0.0
