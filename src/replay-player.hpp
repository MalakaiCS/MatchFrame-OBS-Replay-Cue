/*
Replay Cue for OBS Studio
Copyright (C) 2026 Replay Cue contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#pragma once

#include "time-stretch.hpp"

#include <obs.h>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct AVFrame;

namespace rc {

extern const char *kPlayerSourceId;

struct PlayAngle {
	std::string label;
	std::string file;
	double inOffset = 0; /* seconds from the file's first frame to the IN point */
};

/* What audio does when a replay is not playing at 100% */
enum class SlowAudio {
	Off,     /* silent */
	Natural, /* stretched to the new speed, same pitch */
	Slowed,  /* played slower and lower, like tape */
};

struct PlayRequest {
	std::string eventId;
	std::string eventName;
	std::vector<PlayAngle> angles;
	double duration = 0; /* <= 0: play until the longest angle ends */
	int angle = 0;
	double speed = 1.0;
	double startAt = 0;
	bool autoplay = true;
	bool audio = true;
	SlowAudio slowAudio = SlowAudio::Natural;
	bool hwDecode = true;
};

enum class PlayerState { Idle, Loading, Playing, Paused, Ended, Error };

struct PlayerStatus {
	PlayerState state = PlayerState::Idle;
	std::string eventId;
	std::string eventName;
	std::vector<std::string> angleLabels;
	int angle = 0;
	double speed = 1.0;
	double position = 0;
	double duration = 0;
	double frameDuration = 1.0 / 60.0;
	uint64_t framesShown = 0; /* frames output since the event loaded */
	std::string error;
};

const char *playerStateName(PlayerState state);

class AngleDecoder;

/*
 * The "Replay Cue Player" source. Decodes every angle of an event in lockstep
 * on one shared clock and shows the selected one, so switching angles
 * mid-replay is instant and frame-accurate. Slow motion simply runs the clock
 * slower; its audio is time-stretched, slowed down, or muted (SlowAudio).
 *
 * All public methods are thread-safe.
 */
class ReplayPlayer {
public:
	explicit ReplayPlayer(obs_source_t *source);
	~ReplayPlayer();

	void load(const PlayRequest &request);
	void play();
	void pause();
	void togglePause();
	/* clearFrame: also blank the source (used after leaving the replay scene) */
	void stop(bool clearFrame = false);
	void restart();
	void setAngle(int angle);
	void setSpeed(double speed);
	void seek(double position);
	void step(int frames);
	void setHoldLastFrame(bool hold);

	PlayerStatus status() const;
	obs_source_t *source() const { return obsSource; }

private:
	void run();
	void doLoad(const PlayRequest &request);
	void doStop();
	void tick();
	/* ahead: seconds until the frame's first sample is due on the speaker */
	void outputStretched(const AVFrame *frame, double ahead, double speedNow);
	double clockLocked(int64_t now) const;
	void seekLocked(double position);

	obs_source_t *obsSource;

	mutable std::mutex mutex;
	std::condition_variable cv;
	std::thread thread;
	bool quit = false;
	std::unique_ptr<PlayRequest> pendingLoad;
	bool pendingStop = false;
	bool pendingClear = false;

	std::vector<std::unique_ptr<AngleDecoder>> decoders;
	std::vector<AVFrame *> current; /* latest frame per angle (clock thread only) */
	PlayerState state = PlayerState::Idle;
	std::string eventId;
	std::string eventName;
	std::string error;
	int selected = 0;
	bool angleDirty = false;
	double speed = 1.0;
	double duration = 0;
	double frameDuration = 1.0 / 60.0;
	double clockBase = 0;   /* event time when the clock was last re-based */
	int64_t clockStart = 0; /* nowUsec() at that moment */
	bool waitingForFrames = false;
	int64_t waitStarted = 0;
	uint64_t generation = 0;
	bool playAudio = true;
	SlowAudio slowAudio = SlowAudio::Natural;
	bool holdLastFrame = true;
	bool emitStarted = false;
	uint64_t framesShown = 0;

	/* slow-motion audio (player thread only): restarted whenever the
	 * playback it follows jumps (seek, angle, speed, pause) */
	TimeStretcher stretcher;
	bool stretching = false;
	uint64_t stretchGeneration = 0;
	int stretchAngle = -1;
	double stretchSpeed = 0;
	uint64_t stretchStartNs = 0; /* when the first stretched sample plays */
	uint64_t stretchFrames = 0;  /* stretched frames output since then */
	std::vector<float> stretchIn;
	std::vector<float> stretchOut;
};

void registerPlayerSource();

/* The ReplayPlayer behind a "Replay Cue Player" source, or nullptr. Valid for
 * as long as the caller holds a reference to the source. */
ReplayPlayer *playerFromSource(obs_source_t *source);

} // namespace rc
