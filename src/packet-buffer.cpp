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

#include "packet-buffer.hpp"

#include <cstring>
#include <limits>

namespace rc {

PacketList::PacketList(PacketList &&other) noexcept : info(std::move(other.info)), packets(std::move(other.packets))
{
	other.packets.clear();
}

PacketList &PacketList::operator=(PacketList &&other) noexcept
{
	if (this != &other) {
		clear();
		info = std::move(other.info);
		packets = std::move(other.packets);
		other.packets.clear();
	}
	return *this;
}

PacketList::~PacketList()
{
	clear();
}

void PacketList::clear()
{
	for (TimedPacket &p : packets)
		obs_encoder_packet_release(&p.pkt);
	packets.clear();
}

PacketBuffer::PacketBuffer(bool video_) : video(video_)
{
	info.video = video_;
}

PacketBuffer::~PacketBuffer()
{
	clear();
}

void PacketBuffer::setMaxDurationUsec(int64_t usec)
{
	std::lock_guard<std::mutex> lock(mutex);
	maxDurationUsec = usec;
	trimLocked();
}

void PacketBuffer::setStreamInfo(const StreamInfo &newInfo)
{
	std::lock_guard<std::mutex> lock(mutex);
	std::vector<uint8_t> extradata = std::move(info.extradata);
	info = newInfo;
	info.video = video;
	if (info.extradata.empty())
		info.extradata = std::move(extradata);
}

StreamInfo PacketBuffer::streamInfo() const
{
	std::lock_guard<std::mutex> lock(mutex);
	return info;
}

/* Outputs receive the encoder's own buffer, which is not reference counted, so
 * obs_encoder_packet_ref() must not be used on it. Make a counted copy in the
 * layout libobs uses (a long refcount right before the data) so that
 * obs_encoder_packet_ref/obs_encoder_packet_release work on it afterwards. */
static void copyPacket(encoder_packet &dst, const encoder_packet &src)
{
	dst = src;
	long *refs = static_cast<long *>(bmalloc(src.size + sizeof(long)));
	*refs = 1;
	dst.data = reinterpret_cast<uint8_t *>(refs + 1);
	memcpy(dst.data, src.data, src.size);
}

static int64_t ticksToUsec(int64_t ticks, int32_t num, int32_t den)
{
	if (den == 0)
		return 0;
	return ticks * 1000000LL * num / den;
}

void PacketBuffer::push(encoder_packet *pkt)
{
	if (!pkt || !pkt->data || !pkt->size)
		return;

	std::lock_guard<std::mutex> lock(mutex);

	if (!haveFirst) {
		/* a decodable range has to start on a keyframe */
		if (video && !pkt->keyframe)
			return;
		haveFirst = true;
		firstDts = pkt->dts;
	}

	if (!haveExtradata && pkt->encoder) {
		uint8_t *data = nullptr;
		size_t size = 0;
		if (obs_encoder_get_extra_data(pkt->encoder, &data, &size) && data && size) {
			info.extradata.assign(data, data + size);
			haveExtradata = true;
		}
	}

	/* libobs stamps dts_usec on the shared system clock (see
	 * send_off_encoder_packet). It maps the encoder's first dts to the first
	 * frame's capture time, so shift by the reorder delay to get the capture
	 * time of this packet's frame. */
	TimedPacket timed;
	copyPacket(timed.pkt, *pkt);
	timed.dtsUsec = pkt->dts_usec;
	timed.ptsUsec =
		pkt->dts_usec + ticksToUsec(pkt->pts - pkt->dts + firstDts, pkt->timebase_num, pkt->timebase_den);

	bytes += pkt->size;
	packets.push_back(timed);
	trimLocked();
}

void PacketBuffer::trimLocked()
{
	if (packets.empty())
		return;

	const int64_t newest = packets.back().dtsUsec;
	auto popFront = [this]() {
		bytes -= packets.front().pkt.size;
		obs_encoder_packet_release(&packets.front().pkt);
		packets.pop_front();
	};

	while (!packets.empty() && newest - packets.front().dtsUsec > maxDurationUsec)
		popFront();

	if (video) {
		while (!packets.empty() && !packets.front().pkt.keyframe)
			popFront();
	}
}

void PacketBuffer::clear()
{
	std::lock_guard<std::mutex> lock(mutex);
	for (TimedPacket &p : packets)
		obs_encoder_packet_release(&p.pkt);
	packets.clear();
	bytes = 0;
	haveFirst = false;
	firstDts = 0;
}

bool PacketBuffer::snapshot(int64_t fromUsec, int64_t toUsec, PacketList &out) const
{
	out.clear();

	std::lock_guard<std::mutex> lock(mutex);
	out.info = info;
	if (packets.empty())
		return false;

	auto copy = [&out](const TimedPacket &p) {
		TimedPacket c;
		obs_encoder_packet_ref(&c.pkt, const_cast<encoder_packet *>(&p.pkt));
		c.ptsUsec = p.ptsUsec;
		c.dtsUsec = p.dtsUsec;
		out.packets.push_back(c);
	};

	if (video) {
		size_t start = 0;
		for (size_t i = 0; i < packets.size(); i++) {
			const TimedPacket &p = packets[i];
			if (p.ptsUsec > fromUsec)
				break;
			if (p.pkt.keyframe)
				start = i;
		}
		for (size_t i = start; i < packets.size(); i++) {
			if (packets[i].dtsUsec > toUsec)
				break;
			copy(packets[i]);
		}
	} else {
		const int64_t slack = 50000;
		for (const TimedPacket &p : packets) {
			if (p.ptsUsec < fromUsec - slack)
				continue;
			if (p.ptsUsec > toUsec + slack)
				break;
			copy(p);
		}
	}

	return !out.packets.empty();
}

int64_t PacketBuffer::newestUsec() const
{
	std::lock_guard<std::mutex> lock(mutex);
	return packets.empty() ? std::numeric_limits<int64_t>::min() : packets.back().ptsUsec;
}

int64_t PacketBuffer::oldestUsec() const
{
	std::lock_guard<std::mutex> lock(mutex);
	return packets.empty() ? std::numeric_limits<int64_t>::max() : packets.front().ptsUsec;
}

size_t PacketBuffer::byteSize() const
{
	std::lock_guard<std::mutex> lock(mutex);
	return bytes;
}

bool PacketBuffer::empty() const
{
	std::lock_guard<std::mutex> lock(mutex);
	return packets.empty();
}

} // namespace rc
