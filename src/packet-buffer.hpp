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
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace rc {

struct StreamInfo {
	bool video = true;
	std::string codec; /* "h264", "hevc", "av1", "aac", "opus" */
	std::vector<uint8_t> extradata;
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t fpsNum = 0;
	uint32_t fpsDen = 0;
	enum video_colorspace colorspace = VIDEO_CS_DEFAULT; /* tagged in the files */
	bool fullRange = false;
	uint32_t sampleRate = 0;
	uint32_t channels = 0;
};

/* One encoded packet plus its capture time on the libobs clock. */
struct TimedPacket {
	encoder_packet pkt = {}; /* holds a libobs packet reference */
	int64_t ptsUsec = 0;     /* when the frame was captured */
	int64_t dtsUsec = 0;     /* decode order, same clock */
};

/* A move-only list of referenced packets cut out of a PacketBuffer. */
class PacketList {
public:
	PacketList() = default;
	PacketList(const PacketList &) = delete;
	PacketList &operator=(const PacketList &) = delete;
	PacketList(PacketList &&other) noexcept;
	PacketList &operator=(PacketList &&other) noexcept;
	~PacketList();

	void clear();

	StreamInfo info;
	std::vector<TimedPacket> packets;
};

/*
 * Rolling window of encoded packets for one camera angle (or for the shared
 * audio track). Written from the encoder thread, read from the clip writer.
 *
 * Video buffers always begin on a keyframe, so any range cut out of them can be
 * decoded on its own without re-encoding.
 */
class PacketBuffer {
public:
	explicit PacketBuffer(bool video);
	~PacketBuffer();

	PacketBuffer(const PacketBuffer &) = delete;
	PacketBuffer &operator=(const PacketBuffer &) = delete;

	bool isVideo() const { return video; }

	void setMaxDurationUsec(int64_t usec);
	void setStreamInfo(const StreamInfo &info);
	StreamInfo streamInfo() const;

	void push(encoder_packet *pkt);
	void clear();

	/* Copies the packets needed to show [fromUsec, toUsec]. Video starts at the
	 * last keyframe at or before fromUsec. Returns false if nothing overlaps. */
	bool snapshot(int64_t fromUsec, int64_t toUsec, PacketList &out) const;

	int64_t newestUsec() const;
	int64_t oldestUsec() const;
	size_t byteSize() const;
	bool empty() const;

private:
	void trimLocked();

	const bool video;
	mutable std::mutex mutex;
	std::deque<TimedPacket> packets;
	StreamInfo info;
	int64_t maxDurationUsec = 120 * 1000000LL;
	size_t bytes = 0;
	bool haveFirst = false;
	int64_t firstDts = 0;
	bool haveExtradata = false;
};

} // namespace rc
