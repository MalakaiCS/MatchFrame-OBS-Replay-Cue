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

#include <obs.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rc {

struct CleanFeedSettings {
	std::string sceneName;
	std::string path;      /* the file to write (.mkv or .mp4) */
	std::string encoderId; /* empty: the best available */
	int bitrateKbps = 12000;
	int audioTrack = 2; /* OBS mixer track 1-6, 0: no sound */
	bool hdr = true;    /* 10-bit HDR when the canvas is HDR and the encoder can */
};

/*
 * Records one scene to a file, apart from the programme: the live picture
 * and its graphics, without the replays cut into the programme. The scene
 * is drawn off-screen, the way a projector shows it, so it keeps going while
 * a replay is on air. Its sound is an OBS mixer track. UI thread only.
 */
class CleanFeedRecorder {
public:
	CleanFeedRecorder();
	~CleanFeedRecorder();
	CleanFeedRecorder(const CleanFeedRecorder &) = delete;
	CleanFeedRecorder &operator=(const CleanFeedRecorder &) = delete;

	bool start(const CleanFeedSettings &settings, std::string &error);
	/* the file is finished in the background (tick() tidies up) */
	void stop();
	/* tidies up stopped recordings; true (with `error`) if the one
	 * recording has failed, which also stops it */
	bool tick(std::string &error);
	/* OBS is closing: finish the files before returning */
	void shutdown();

	bool recording() const { return active != nullptr; }
	std::string file() const;
	std::string scene() const;
	double seconds() const;

private:
	struct Session;
	std::unique_ptr<Session> active;
	std::vector<std::unique_ptr<Session>> finishing;
};

} // namespace rc
