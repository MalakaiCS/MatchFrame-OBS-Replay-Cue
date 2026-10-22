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

#include "packet-buffer.hpp"

#include <obs.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace rc {

struct CaptureSettings {
	std::string encoderId; /* empty: pick the best available */
	int bitrateKbps = 25000;
	int keyintSec = 1;
	int maxHeight = 1080; /* 0: keep the source's own size */
	int bufferSeconds = 120;
	int audioTrack = 1; /* 0: no audio, 1-6: OBS mixer track */
};

struct AngleSpec {
	std::string label;
	std::string sourceName;
};

struct AngleStatus {
	std::string label;
	std::string sourceName;
	bool running = false;
	std::string state;
	double bufferedSeconds = 0;
	size_t bytes = 0;
	uint32_t width = 0;
	uint32_t height = 0;
};

/* Renders one OBS source off-screen, encodes it and keeps the last N seconds
 * of packets in memory. Must be driven from the UI thread. */
class AngleCapture {
public:
	AngleCapture(AngleSpec spec, const CaptureSettings &settings);
	~AngleCapture();

	AngleCapture(const AngleCapture &) = delete;
	AngleCapture &operator=(const AngleCapture &) = delete;

	void tick();
	void stop();

	AngleStatus status() const;
	std::shared_ptr<PacketBuffer> buffer() const { return packets; }
	const AngleSpec &spec() const { return angle; }

private:
	bool start(obs_source_t *source);
	void fail(const std::string &why);

	AngleSpec angle;
	CaptureSettings settings;
	std::shared_ptr<PacketBuffer> packets;
	obs_view_t *view = nullptr;
	video_t *video = nullptr;
	obs_encoder_t *encoder = nullptr;
	obs_output_t *output = nullptr;
	uint32_t baseWidth = 0;
	uint32_t baseHeight = 0;
	uint32_t outWidth = 0;
	uint32_t outHeight = 0;
	std::string state;
	int64_t retryAfterUsec = 0;
};

/* AAC (or Opus) encode of one OBS mixer track, shared by every angle. */
class AudioCapture {
public:
	AudioCapture(int track, int bufferSeconds);
	~AudioCapture();

	AudioCapture(const AudioCapture &) = delete;
	AudioCapture &operator=(const AudioCapture &) = delete;

	bool running() const { return output != nullptr; }
	std::shared_ptr<PacketBuffer> buffer() const { return packets; }

private:
	std::shared_ptr<PacketBuffer> packets;
	obs_encoder_t *encoder = nullptr;
	obs_output_t *output = nullptr;
};

class CaptureEngine {
public:
	~CaptureEngine();

	void arm(const std::vector<AngleSpec> &angles, const CaptureSettings &settings);
	void disarm();
	bool armed() const { return isArmed; }
	void tick();

	std::vector<AngleStatus> status() const;

	struct BufferRef {
		std::string label;
		std::shared_ptr<PacketBuffer> buffer;
	};
	std::vector<BufferRef> videoBuffers() const;
	std::shared_ptr<PacketBuffer> audioBuffer() const;

	/* Oldest moment every running angle can still provide. */
	int64_t oldestCommonUsec() const;

	static std::vector<std::pair<std::string, std::string>> availableVideoEncoders();
	static std::string defaultVideoEncoder();
	static std::string resolveVideoEncoder(const std::string &preferred);

private:
	bool isArmed = false;
	CaptureSettings current;
	std::vector<std::unique_ptr<AngleCapture>> angles;
	std::unique_ptr<AudioCapture> audio;
};

} // namespace rc
