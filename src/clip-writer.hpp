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

#include "capture-engine.hpp"
#include "packet-buffer.hpp"
#include "replay-event.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace rc {

/* Everything needed to turn the live buffers into a replay event on disk. */
struct ClipJob {
	int number = 0;
	std::string name;
	std::string tag;
	std::string folder;
	std::string baseName;
	int64_t created = 0;
	int64_t inUsec = 0;
	int64_t outUsec = 0;
	int64_t tailUsec = 0; /* extra media kept after OUT so the last frame decodes */
	std::vector<CaptureEngine::BufferRef> angles;
	std::shared_ptr<PacketBuffer> audio;
	bool playWhenReady = false;
};

struct ClipResult {
	bool ok = false;
	std::string error;
	ReplayEvent event;
	bool playWhenReady = false;
};

struct ExportItem {
	std::string input;
	std::string output;
	double start = 0; /* seconds in the input file */
	double end = 0;   /* <= start: to the end of the file */
};

/* Muxes a packet range to an MP4 file. firstFrameUsec receives the capture
 * time of the first frame in the file. */
bool writeClipFile(const std::string &path, const PacketList &video, const PacketList *audio, int64_t &firstFrameUsec,
		   double &duration, std::string &error);

/* Stream-copies [start, end] of a clip into a web-friendly MP4 (moov atom at
 * the front). Starts on the keyframe at or before `start`. */
bool exportClip(const ExportItem &item, std::string &error);

/* Single background thread that writes clips and exports in order. */
class ClipWriter {
public:
	using ClipCallback = std::function<void(ClipResult)>;
	using ExportCallback = std::function<void(bool ok, std::string message)>;

	explicit ClipWriter(ClipCallback onClip);
	~ClipWriter();

	void enqueue(ClipJob job);
	void enqueueExport(std::vector<ExportItem> items, ExportCallback done);

private:
	void run();
	ClipResult process(ClipJob &job);

	ClipCallback onClip;
	std::mutex mutex;
	std::condition_variable cv;
	std::deque<std::function<void()>> tasks;
	std::atomic<bool> quitting{false};
	std::thread worker;
};

} // namespace rc
