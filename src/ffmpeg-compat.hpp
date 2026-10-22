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

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include <string>

namespace rc {

/* av_err2str() relies on a C compound literal, which C++ (and MSVC) reject. */
inline std::string averr(int err)
{
	char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
	av_strerror(err, buf, sizeof(buf));
	return buf;
}

/* The plugin uses the FFmpeg libraries that ship inside OBS. FFmpeg structs
 * change layout between major versions, so refuse to touch them when the
 * runtime libraries are not the ones this plugin was compiled against. */
bool ffmpegAbiCompatible(std::string &details);

/* Set once at module load; false disables recording, playback and probing. */
bool ffmpegReady();
void setFfmpegReady(bool ready);

enum AVCodecID codecIdFromObs(const char *obsCodec);

} // namespace rc
