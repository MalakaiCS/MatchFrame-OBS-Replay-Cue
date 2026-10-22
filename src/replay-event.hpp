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
#include <map>
#include <string>
#include <vector>

namespace rc {

extern const char *kSidecarExtension; /* ".rcue.json" */

/* One camera angle of a replay: a media file and where it sits in time. */
struct AngleClip {
	std::string label;
	std::string file;           /* absolute path */
	int64_t firstFrameUsec = 0; /* capture time of the file's first frame, 0 if unknown */
	double duration = 0;        /* seconds */
};

/*
 * A replay "event": the same moment seen from up to eight angles.
 *
 * Events recorded by Replay Cue know the exact capture time of every file, so
 * all angles line up frame-for-frame. Events built from loose files in the
 * replay folder (Source Record, the OBS Replay Buffer, another machine) are
 * aligned at their first frame.
 */
struct ReplayEvent {
	std::string id;
	std::string name;
	std::string tag;
	std::string sidecar; /* path of the .rcue.json file, empty for loose files */
	int number = 0;      /* 0 for imported events */
	int64_t created = 0; /* wall clock seconds */
	int64_t inUsec = 0;  /* marked in/out on the libobs clock, 0 if imported */
	int64_t outUsec = 0;
	double duration = 0; /* playable length in seconds */
	bool imported = false;
	bool favorite = false;
	bool highlight = false; /* in the highlights reels */
	int period = 0;         /* match period it was saved in, 0 if none */
	std::string match;      /* match it belongs to (Config::matchId) */
	std::vector<AngleClip> angles;

	/* Seconds from the start of angle i's file to the IN point. */
	double inOffset(size_t angle) const;
	std::vector<std::string> angleLabels() const;
};

bool saveSidecar(const ReplayEvent &event, std::string &error);
bool loadSidecar(const std::string &path, ReplayEvent &event);
obs_data_t *eventToData(const ReplayEvent &event); /* for websocket replies */

/* Probes a media file's duration in seconds (0 on failure). */
double probeDuration(const std::string &path);

/* Duration cache so rescans only probe new or changed files. */
class ProbeCache {
public:
	double duration(const std::string &path);

private:
	struct Entry {
		int64_t size = 0;
		int64_t mtime = 0;
		double duration = 0;
	};
	std::map<std::string, Entry> entries;
};

/*
 * Builds the event list for a replay folder: every sidecar file, plus loose
 * media files grouped into multi-angle events by camera folder or file name
 * ("Cam1/goal.mp4" + "Cam2/goal.mp4", or "goal_Cam1.mp4" + "goal_Cam2.mp4").
 */
std::vector<ReplayEvent> scanReplayFolder(const std::string &folder, const std::vector<std::string> &angleLabels,
					  ProbeCache &cache);

bool isMediaFile(const std::string &path);

} // namespace rc
