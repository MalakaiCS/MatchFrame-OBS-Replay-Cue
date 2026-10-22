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

#include "rc-common.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace rc {

int64_t wallClockSeconds()
{
	using namespace std::chrono;
	return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

double monotonicToWallClock(int64_t usec)
{
	using namespace std::chrono;
	double wallNow = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count() / 1e6;
	return wallNow - (double)(nowUsec() - usec) / 1e6;
}

std::string sanitizeFileName(const std::string &name)
{
	std::string out;
	out.reserve(name.size());
	for (unsigned char c : name) {
		if (c < 32 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
		    c == '>' || c == '|')
			out.push_back('_');
		else if (c == ' ')
			out.push_back('-');
		else
			out.push_back((char)c);
	}
	while (!out.empty() && (out.back() == '.' || out.back() == '-'))
		out.pop_back();
	if (out.empty())
		out = "clip";
	return out;
}

std::string normalizeTag(const std::string &name)
{
	std::string out;
	for (unsigned char c : name) {
		if (c >= 'A' && c <= 'Z')
			out.push_back((char)(c - 'A' + 'a'));
		else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
			out.push_back((char)c);
	}
	return out;
}

std::string formatDuration(double seconds)
{
	if (!std::isfinite(seconds) || seconds < 0)
		seconds = 0;
	int total = (int)seconds;
	int minutes = total / 60;
	double rest = seconds - minutes * 60;
	char buf[32];
	snprintf(buf, sizeof(buf), "%02d:%05.2f", minutes, rest);
	return buf;
}

static bool localTime(int64_t unixSeconds, struct tm &out)
{
	time_t t = (time_t)unixSeconds;
#ifdef _WIN32
	return localtime_s(&out, &t) == 0;
#else
	return localtime_r(&t, &out) != nullptr;
#endif
}

std::string formatClock(int64_t unixSeconds)
{
	struct tm tm = {};
	if (!localTime(unixSeconds, tm))
		return "--:--:--";
	char buf[16];
	strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
	return buf;
}

std::string formatFileStamp(int64_t unixSeconds)
{
	struct tm tm = {};
	if (!localTime(unixSeconds, tm))
		return "replay";
	char buf[32];
	strftime(buf, sizeof(buf), "%Y-%m-%d_%H-%M-%S", &tm);
	return buf;
}

static size_t lastSeparator(const std::string &path)
{
	size_t a = path.find_last_of('/');
	size_t b = path.find_last_of('\\');
	if (a == std::string::npos)
		return b;
	if (b == std::string::npos)
		return a;
	return a > b ? a : b;
}

std::string joinPath(const std::string &dir, const std::string &file)
{
	if (dir.empty())
		return file;
	char last = dir.back();
	if (last == '/' || last == '\\')
		return dir + file;
	return dir + "/" + file;
}

std::string fileNameOf(const std::string &path)
{
	size_t sep = lastSeparator(path);
	return sep == std::string::npos ? path : path.substr(sep + 1);
}

std::string dirNameOf(const std::string &path)
{
	size_t sep = lastSeparator(path);
	return sep == std::string::npos ? std::string() : path.substr(0, sep);
}

std::string stemOf(const std::string &path)
{
	std::string name = fileNameOf(path);
	size_t dot = name.find_last_of('.');
	return (dot == std::string::npos || dot == 0) ? name : name.substr(0, dot);
}

std::string extensionOf(const std::string &path)
{
	std::string name = fileNameOf(path);
	size_t dot = name.find_last_of('.');
	if (dot == std::string::npos)
		return std::string();
	std::string ext = name.substr(dot + 1);
	for (char &c : ext)
		if (c >= 'A' && c <= 'Z')
			c = (char)(c - 'A' + 'a');
	return ext;
}

const char *text(const char *lookup)
{
	return obs_module_text(lookup);
}

} // namespace rc
