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

#include "match.hpp"
#include "rc-common.hpp"

#include <obs-data.h>

#include <cstring>

namespace rc {

const char *kMatchFileName = "match.rcmatch.json";
const char *kHighlightsFolder = "Highlights";

namespace {

std::string teams(const MatchInfo &match)
{
	if (match.home.empty() || match.away.empty())
		return match.home.empty() ? match.away : match.home;
	return match.home + " " + text("Match.Versus") + " " + match.away;
}

/* "2026-10-22_14-03-22" -> "2026-10-22 14:03" */
std::string prettyStamp(const std::string &stamp)
{
	if (stamp.size() < 16 || stamp[10] != '_')
		return stamp;
	return stamp.substr(0, 10) + " " + stamp.substr(11, 2) + ":" + stamp.substr(14, 2);
}

} // namespace

std::string matchTitle(const MatchInfo &match)
{
	const std::string versus = teams(match);
	if (!match.name.empty() && !versus.empty())
		return match.name + ": " + versus;
	if (!match.name.empty() || !versus.empty())
		return match.name.empty() ? versus : match.name;
	return std::string(text("Match.Unnamed")) + " " + prettyStamp(match.id);
}

std::string matchFolderName(const MatchInfo &match)
{
	const std::string versus = teams(match);
	if (!match.name.empty() && !versus.empty())
		return safeName(match.name + " - " + versus);
	return safeName(matchTitle(match));
}

std::string safeName(const std::string &name)
{
	std::string out;
	out.reserve(name.size());
	for (unsigned char c : name) {
		if (c < 32 || strchr("/\\:*?\"<>|", c))
			out.push_back('-');
		else
			out.push_back((char)c);
	}
	/* Windows drops trailing dots and spaces, and very long paths fail */
	if (out.size() > 80) {
		size_t cut = 80;
		while (cut > 0 && ((unsigned char)out[cut] & 0xC0) == 0x80)
			cut--; /* not inside a UTF-8 character */
		out.resize(cut);
	}
	while (!out.empty() && (out.back() == '.' || out.back() == ' '))
		out.pop_back();
	while (!out.empty() && out.front() == ' ')
		out.erase(out.begin());
	return out.empty() ? std::string("Match") : out;
}

bool saveMatchFile(const MatchInfo &match, std::string &error)
{
	if (match.folder.empty()) {
		error = "the match has no folder";
		return false;
	}
	obs_data_t *data = obs_data_create();
	obs_data_set_string(data, "format", "replay-cue-match");
	obs_data_set_int(data, "version", 1);
	obs_data_set_string(data, "id", match.id.c_str());
	obs_data_set_string(data, "name", match.name.c_str());
	obs_data_set_string(data, "home", match.home.c_str());
	obs_data_set_string(data, "away", match.away.c_str());
	obs_data_set_string(data, "sport", match.sport.c_str());
	obs_data_set_int(data, "phase", match.phase);
	obs_data_set_int(data, "created", match.created);

	const std::string path = joinPath(match.folder, kMatchFileName);
	const bool ok = obs_data_save_json_pretty_safe(data, path.c_str(), ".tmp", nullptr);
	obs_data_release(data);
	if (!ok)
		error = "could not write " + path;
	return ok;
}

bool loadMatchFile(const std::string &folder, MatchInfo &match)
{
	obs_data_t *data = obs_data_create_from_json_file(joinPath(folder, kMatchFileName).c_str());
	if (!data)
		return false;
	const bool ok = strcmp(obs_data_get_string(data, "format"), "replay-cue-match") == 0 &&
			*obs_data_get_string(data, "id");
	if (ok) {
		match = MatchInfo();
		match.id = obs_data_get_string(data, "id");
		match.name = obs_data_get_string(data, "name");
		match.home = obs_data_get_string(data, "home");
		match.away = obs_data_get_string(data, "away");
		match.sport = obs_data_get_string(data, "sport");
		match.phase = (int)obs_data_get_int(data, "phase");
		match.created = obs_data_get_int(data, "created");
		match.folder = folder;
	}
	obs_data_release(data);
	return ok;
}

} // namespace rc
