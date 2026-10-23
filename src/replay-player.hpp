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
struct SwsContext;

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
	uint64_t fades = 0;       /* times the player faded into the next replay by itself */
	std::string error;
};

const char *playerStateName(PlayerState state);

class AngleDecoder;
struct AudioRamp;

/*
 * The "Replay Cue Player" source. Decodes every angle of an event in lockstep
 * on one shared clock and shows the selected one, so switching angles
 * mid-replay is instant and frame-accurate. Slow motion simply runs the clock
 * slower; its audio is time-stretched, slowed down, or muted (SlowAudio).
 * A highlights reel cross-fades from one replay into the next (setNext).
 *
 * All public methods are thread-safe.
 */
class ReplayPlayer {
public:
	explicit ReplayPlayer(obs_source_t *source);
	~ReplayPlayer();

	void load(const PlayRequest &request);
	/* The replay to fade into `fade` seconds before this one ends. It is
	 * opened in the background now, so the fade starts on time; load(),
	 * stop() and clearNext() drop it. */
	void setNext(const PlayRequest &request, double fade);
	void clearNext();
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
	void outputStretched(const AVFrame *frame, double ahead, double speedNow, const AudioRamp *ramp);
	void startFadeLocked(int64_t now, std::vector<std::unique_ptr<AngleDecoder>> &retired);
	void dropNextLocked(std::vector<std::unique_ptr<AngleDecoder>> &retired);
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
	std::vector<float> rampBuffer;

	/* the next replay, opened ahead (setNext) */
	std::unique_ptr<PlayRequest> nextRequest;
	std::vector<std::unique_ptr<AngleDecoder>> nextDecoders;
	bool nextReady = false; /* nextDecoders are open and decoding */
	double nextFade = 0;
	double nextLongest = 0;
	double nextFrameDuration = 0;
	uint64_t nextGeneration = 0;
	uint64_t nextToken = 0; /* bumped to throw away a preload still opening */
	uint64_t generationCounter = 0;
	std::thread preloader;

	/* the replay fading out (clock thread only, but set up under the mutex) */
	std::unique_ptr<AngleDecoder> outgoing;
	AVFrame *outFrame = nullptr;
	bool outDirty = false;
	double outBase = 0;   /* its position when the fade started */
	int64_t outStart = 0; /* nowUsec() then */
	double outSpeed = 1.0;
	double outOffset = 0;
	uint64_t outGeneration = 0;
	int outAngle = 0;
	int64_t fadeStart = 0;  /* nowUsec() */
	int64_t fadeLength = 0; /* microseconds */
	bool fadeAbort = false; /* pause, seek: finish the fade at once */
	int64_t lastFadeOutput = 0;
	uint64_t fades = 0;
	AVFrame *blendFrame = nullptr;
	AVFrame *blendSource = nullptr;
	SwsContext *blendSws = nullptr;
};

void registerPlayerSource();

/* The ReplayPlayer behind a "Replay Cue Player" source, or nullptr. Valid for
 * as long as the caller holds a reference to the source. */
ReplayPlayer *playerFromSource(obs_source_t *source);

} // namespace rc
