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

#include "capture-engine.hpp"

#include <string>
#include <vector>

namespace rc {

struct AngleSetting {
	std::string label;
	std::string source;
	bool enabled = true;
};

struct Config {
	/* cameras */
	std::vector<AngleSetting> angles;

	/* capture */
	bool armOnStartup = false;
	int bufferSeconds = 120;
	std::string encoderId;
	int bitrateKbps = 25000;
	int keyintSec = 1;
	int maxHeight = 1080;
	int audioTrack = 1;

	/* replays */
	std::string folder;
	bool watchFolder = true;
	int instantSeconds = 8;
	int tailMs = 300;
	int markLatencyMs = 0;
	bool importReplayBuffer = true;
	std::string exportFolder;
	std::vector<std::string> tags;

	/* playback */
	std::string sceneName = "Replay";
	std::string playerName = "Replay Cue Player";
	std::string replayTransition;
	std::string returnTransition;
	std::string liveScene; /* empty: return to whatever was live before */
	double defaultSpeed = 1.0;
	bool autoPlayNext = false;
	bool cycleAngles = false;
	bool returnToLive = true;
	bool playAudio = true;
	bool hwDecode = true;
	bool startCuedOnLive = true;

	void load();
	void save() const;

	std::vector<AngleSpec> enabledAngles() const;
	CaptureSettings captureSettings() const;
	std::vector<std::string> angleLabels() const;

	static std::string configPath();
	static std::string defaultFolder();
};

} // namespace rc
