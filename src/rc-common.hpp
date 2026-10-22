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

#include <obs-module.h>
#include <plugin-support.h>
#include <util/platform.h>

#include <cstdint>
#include <string>
#include <vector>

namespace rc {

constexpr int kMaxAngles = 8;

/* All capture and playback timing uses the libobs monotonic clock in
 * microseconds. Encoder packets carry timestamps on this same clock, which is
 * what keeps every camera angle frame-aligned. */
inline int64_t nowUsec()
{
	return (int64_t)(os_gettime_ns() / 1000);
}

/* Wall clock (UTC seconds since epoch), used for display and file names. */
int64_t wallClockSeconds();

/* Converts a libobs monotonic timestamp into wall clock seconds. */
double monotonicToWallClock(int64_t usec);

std::string sanitizeFileName(const std::string &name);

/* Lower-case alphanumerics only: "Cam 1" -> "cam1". Used to match camera
 * labels against folder and file names. */
std::string normalizeTag(const std::string &name);

/* "12.4" -> "00:12.40", "75" -> "01:15.00" */
std::string formatDuration(double seconds);

/* Local time of day "15:30:12" for a wall clock timestamp. */
std::string formatClock(int64_t unixSeconds);

/* "2026-10-22_15-30-12" for a wall clock timestamp. */
std::string formatFileStamp(int64_t unixSeconds);

std::string joinPath(const std::string &dir, const std::string &file);
std::string fileNameOf(const std::string &path);
std::string dirNameOf(const std::string &path);
std::string stemOf(const std::string &path);
std::string extensionOf(const std::string &path);

const char *text(const char *lookup);

} // namespace rc
