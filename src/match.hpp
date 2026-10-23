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

#include <cstdint>
#include <string>

namespace rc {

extern const char *kMatchFileName;    /* "match.rcmatch.json", in the match's folder */
extern const char *kHighlightsFolder; /* "Highlights", where a match's reels are exported */

/*
 * A match ("Match 1: Leeds vs York"). Its replays and exported highlights
 * live in its own folder inside the replay folder, described by a match file
 * there. Matches from before 1.3 have only an id: their replays stay in the
 * replay folder itself.
 */
struct MatchInfo {
	std::string id; /* stamped on its replays (ReplayEvent::match) */
	std::string name;
	std::string home;
	std::string away;
	std::string sport;
	int phase = 0; /* how far the match got (see sport.hpp) */
	int64_t created = 0;
	std::string folder; /* absolute path, empty for a match without its own folder */
};

/* "Match 1: Leeds vs York", "Match 1", "Leeds vs York" */
std::string matchTitle(const MatchInfo &match);

/* Folder (and file name) safe form of the title: "Match 1 - Leeds vs York" */
std::string matchFolderName(const MatchInfo &match);

/* A file or folder name that keeps its spaces: "Half Time: 1/2" -> "Half Time- 1-2" */
std::string safeName(const std::string &name);

bool saveMatchFile(const MatchInfo &match, std::string &error);
bool loadMatchFile(const std::string &folder, MatchInfo &match);

} // namespace rc
