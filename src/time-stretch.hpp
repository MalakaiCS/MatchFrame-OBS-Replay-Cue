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

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rc {

/*
 * Changes the speed of audio without changing its pitch (WSOLA: windowed
 * overlap-add, each window placed where it best continues the last one), so
 * slow-motion replays keep a natural sound. Works on interleaved float
 * samples, in a stream: push input as it is decoded and take the output that
 * is ready.
 */
class TimeStretcher {
public:
	/* tempo: input seconds consumed per output second (the playback speed) */
	void configure(int sampleRate, int channels, double tempo);
	void reset();

	/* Appends `frames` interleaved input frames and appends every output frame
	 * that is complete to `out`. */
	void push(const float *input, size_t frames, std::vector<float> &out);

	int sampleRate() const { return rate; }
	int channels() const { return chans; }
	double tempo() const { return speed; }
	bool configured() const { return rate > 0 && chans > 0; }

private:
	bool step(std::vector<float> &out);
	int64_t bestOffset(int64_t nominal) const;
	float monoAt(int64_t frame) const;

	int rate = 0;
	int chans = 0;
	double speed = 1.0;
	int window = 0; /* N: frames per window */
	int hop = 0;    /* synthesis hop, N / 2 */
	int search = 0; /* how far a window may move to line up */
	std::vector<float> hann;

	std::vector<float> input; /* interleaved, input[0] is frame inputStart */
	int64_t inputStart = 0;
	int64_t inputEnd = 0;
	double nominal = 0;       /* where the next window would start at exactly `speed` */
	int64_t previous = -1;    /* where the last window started */
	std::vector<float> accum; /* overlap-add buffer, `window` frames */
};

} // namespace rc
