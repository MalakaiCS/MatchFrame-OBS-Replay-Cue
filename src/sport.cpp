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

#include "sport.hpp"
#include "rc-common.hpp"

namespace rc {

namespace {

const char *kQuarterBreaks[] = {"Break.QuarterTime", "Break.HalfTime", "Break.ThreeQuarterTime"};
const char *kEndOfQuarterBreaks[] = {"Break.EndQ1", "Break.HalfTime", "Break.EndQ3"};

/* "%1 · %2" style formatting for locale strings outside Qt */
std::string format(const char *pattern, const std::string &a, const std::string &b = std::string())
{
	std::string out = pattern;
	for (auto [key, value] : {std::pair<const char *, const std::string &>{"%1", a}, {"%2", b}}) {
		const size_t at = out.find(key);
		if (at != std::string::npos)
			out.replace(at, 2, value);
	}
	return out;
}

} // namespace

const std::vector<Sport> &sports()
{
	static const std::vector<Sport> list = {
		{"none", "Sport.None", 0, "", {}, {"Goal", "Foul", "Save", "Highlight"}},
		{"football",
		 "Sport.Football",
		 2,
		 "Half",
		 {"Break.HalfTime"},
		 {"Goal", "Shot", "Save", "Foul", "Yellow Card", "Red Card", "Penalty", "Corner", "Offside", "VAR"}},
		{"rugby_union",
		 "Sport.RugbyUnion",
		 2,
		 "Half",
		 {"Break.HalfTime"},
		 {"Try", "Conversion", "Penalty Goal", "Drop Goal", "Line Break", "Tackle", "Turnover", "Scrum",
		  "Lineout", "Card"}},
		{"rugby_league",
		 "Sport.RugbyLeague",
		 2,
		 "Half",
		 {"Break.HalfTime"},
		 {"Try", "Conversion", "Penalty Goal", "Field Goal", "Line Break", "Tackle", "Error", "Sin Bin"}},
		{"afl",
		 "Sport.AFL",
		 4,
		 "Quarter",
		 {std::begin(kQuarterBreaks), std::end(kQuarterBreaks)},
		 {"Goal", "Behind", "Mark", "Contested Mark", "Tackle", "Smother", "Free Kick", "50m Penalty"}},
		{"gaelic", "Sport.Gaelic", 2, "Half", {"Break.HalfTime"}, {"Goal", "Point", "Save", "Free", "Card"}},
		{"netball",
		 "Sport.Netball",
		 4,
		 "Quarter",
		 {std::begin(kQuarterBreaks), std::end(kQuarterBreaks)},
		 {"Goal", "Intercept", "Rebound", "Turnover", "Penalty"}},
		{"hockey",
		 "Sport.Hockey",
		 4,
		 "Quarter",
		 {std::begin(kQuarterBreaks), std::end(kQuarterBreaks)},
		 {"Goal", "Penalty Corner", "Penalty Stroke", "Save", "Card"}},
		{"basketball",
		 "Sport.Basketball",
		 4,
		 "Quarter",
		 {std::begin(kEndOfQuarterBreaks), std::end(kEndOfQuarterBreaks)},
		 {"Dunk", "Three-Pointer", "Block", "Steal", "Assist", "Foul"}},
		{"american_football",
		 "Sport.AmericanFootball",
		 4,
		 "Quarter",
		 {std::begin(kEndOfQuarterBreaks), std::end(kEndOfQuarterBreaks)},
		 {"Touchdown", "Field Goal", "Interception", "Sack", "Fumble", "Big Play"}},
		{"ice_hockey",
		 "Sport.IceHockey",
		 3,
		 "Period",
		 {"Break.Intermission1", "Break.Intermission2"},
		 {"Goal", "Save", "Hit", "Penalty", "Power Play"}},
	};
	return list;
}

const Sport &findSport(const std::string &id)
{
	for (const Sport &sport : sports())
		if (id == sport.id)
			return sport;
	return sports().front();
}

int phaseCount(const Sport &sport)
{
	return sport.periods > 0 ? 2 * sport.periods + 1 : 1;
}

bool isBreakPhase(const Sport &sport, int phase)
{
	return sport.periods > 0 && phase > 0 && phase % 2 == 0 && phase < phaseCount(sport);
}

bool isFullTime(const Sport &sport, int phase)
{
	return sport.periods > 0 && phase == 2 * sport.periods;
}

int periodOfPhase(int phase)
{
	if (phase <= 0)
		return 0;
	return phase % 2 ? (phase + 1) / 2 : phase / 2;
}

std::string periodName(const Sport &sport, int period)
{
	if (period <= 0 || period > sport.periods)
		return std::string();
	return text((std::string("Period.") + sport.periodStyle + std::to_string(period)).c_str());
}

std::string phaseName(const Sport &sport, int phase)
{
	if (phase <= 0 || sport.periods == 0)
		return text("Phase.PreMatch");
	if (isFullTime(sport, phase))
		return text("Break.FullTime");
	if (phase % 2)
		return periodName(sport, periodOfPhase(phase));
	const size_t index = (size_t)(phase / 2 - 1);
	return index < sport.breakKeys.size() ? text(sport.breakKeys[index]) : std::string();
}

std::vector<int> reelsOf(const Sport &sport)
{
	std::vector<int> reels;
	for (int phase = 2; phase < phaseCount(sport); phase += 2)
		reels.push_back(phase);
	if (reels.empty())
		reels.push_back(0);
	return reels;
}

bool reelIncludes(const Sport &sport, int reel, int period)
{
	if (reel <= 0 || isFullTime(sport, reel))
		return true;
	const int finished = reel / 2;
	if (sport.periods % 2 == 0 && finished == sport.periods / 2)
		return period >= 1 && period <= finished; /* half time: the first half */
	return period == finished;
}

std::string reelName(const Sport &sport, int reel)
{
	if (reel <= 0 || sport.periods == 0)
		return text("Reel.All");
	if (isFullTime(sport, reel))
		return format(text("Reel.Format"), phaseName(sport, reel), text("Reel.WholeMatch"));

	const int finished = reel / 2;
	std::string covers;
	if (sport.periods % 2 == 0 && finished == sport.periods / 2 && finished > 1)
		covers = format(text("Reel.Range"), periodName(sport, 1), periodName(sport, finished));
	else
		covers = periodName(sport, finished);
	return format(text("Reel.Format"), phaseName(sport, reel), covers);
}

int reelForPhase(const Sport &sport, int phase)
{
	if (sport.periods == 0)
		return 0;
	if (phase <= 0)
		return 2;
	return phase % 2 ? phase + 1 : phase;
}

} // namespace rc
