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

#include <string>
#include <vector>

namespace rc {

/*
 * A sport's match structure: how many periods it has and what the breaks
 * between them are called. A match with N periods runs through 2N + 1 phases:
 *
 *   0 pre-match, 1 period 1, 2 break after period 1, 3 period 2, ..., 2N full time
 *
 * Replays are stamped with the period they were saved in. Every break, and
 * full time, has a highlights reel of the periods it follows.
 */
struct Sport {
	const char *id;
	const char *nameKey;                 /* locale key of the sport's name */
	int periods;                         /* 0: no match structure */
	const char *periodStyle;             /* "Half", "Quarter" or "Period" */
	std::vector<const char *> breakKeys; /* locale keys of the breaks after periods 1..N-1 */
	std::vector<const char *> tags;      /* quick tags for this sport */
};

const std::vector<Sport> &sports();
const Sport &findSport(const std::string &id); /* unknown ids give the "none" sport */

int phaseCount(const Sport &sport);
bool isBreakPhase(const Sport &sport, int phase); /* a break or full time */
bool isFullTime(const Sport &sport, int phase);

/* The period a replay saved during this phase belongs to: the period in play,
 * or during a break the one that just finished. 0 before kick-off. */
int periodOfPhase(int phase);

std::string periodName(const Sport &sport, int period); /* "1st Half", "Q3"; empty for 0 */
std::string phaseName(const Sport &sport, int phase);   /* "Pre-match", "1st Half", "Half Time"... */

/*
 * Highlights reels are named by the break they are for. Half Time covers the
 * first half, full time the whole match, and any other break the period that
 * just finished. Reel 0 is "all highlights" (sports without periods).
 */
std::vector<int> reelsOf(const Sport &sport);
bool reelIncludes(const Sport &sport, int reel, int period);
std::string reelName(const Sport &sport, int reel);

/* The reel to get ready during this phase: the coming break's while a period
 * is in play, the current break's during a break. */
int reelForPhase(const Sport &sport, int phase);

} // namespace rc
