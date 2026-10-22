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

#include "ffmpeg-compat.hpp"

#include <cstdio>
#include <cstring>

namespace rc {

static bool checkLib(const char *name, unsigned runtime, unsigned compiled, std::string &details)
{
	unsigned runtimeMajor = runtime >> 16;
	unsigned compiledMajor = compiled >> 16;
	if (runtimeMajor == compiledMajor)
		return true;

	char buf[160];
	snprintf(buf, sizeof(buf), "%s %u.%u (OBS) != %u.%u (plugin); ", name, runtimeMajor, (runtime >> 8) & 0xff,
		 compiledMajor, (compiled >> 8) & 0xff);
	details += buf;
	return false;
}

bool ffmpegAbiCompatible(std::string &details)
{
	bool ok = true;
	ok &= checkLib("libavcodec", avcodec_version(), LIBAVCODEC_VERSION_INT, details);
	ok &= checkLib("libavformat", avformat_version(), LIBAVFORMAT_VERSION_INT, details);
	ok &= checkLib("libavutil", avutil_version(), LIBAVUTIL_VERSION_INT, details);
	ok &= checkLib("libswscale", swscale_version(), LIBSWSCALE_VERSION_INT, details);
	return ok;
}

static bool ready = false;

bool ffmpegReady()
{
	return ready;
}

void setFfmpegReady(bool value)
{
	ready = value;
}

enum AVCodecID codecIdFromObs(const char *obsCodec)
{
	if (!obsCodec)
		return AV_CODEC_ID_NONE;
	if (strcmp(obsCodec, "h264") == 0)
		return AV_CODEC_ID_H264;
	if (strcmp(obsCodec, "hevc") == 0)
		return AV_CODEC_ID_HEVC;
	if (strcmp(obsCodec, "av1") == 0)
		return AV_CODEC_ID_AV1;
	if (strcmp(obsCodec, "prores") == 0)
		return AV_CODEC_ID_PRORES;
	if (strcmp(obsCodec, "aac") == 0)
		return AV_CODEC_ID_AAC;
	if (strcmp(obsCodec, "opus") == 0)
		return AV_CODEC_ID_OPUS;
	if (strcmp(obsCodec, "alac") == 0)
		return AV_CODEC_ID_ALAC;
	if (strcmp(obsCodec, "flac") == 0)
		return AV_CODEC_ID_FLAC;
	return AV_CODEC_ID_NONE;
}

} // namespace rc
