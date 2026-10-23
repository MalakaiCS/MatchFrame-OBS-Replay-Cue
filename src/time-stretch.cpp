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

#include "time-stretch.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace rc {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

void TimeStretcher::configure(int sampleRate, int channelCount, double tempo)
{
	rate = std::max(1, sampleRate);
	chans = std::max(1, channelCount);
	speed = std::clamp(tempo, 0.05, 4.0);
	/* 40 ms windows overlapping by half; each may slide 12 ms to line up */
	window = std::max(64, (int)std::lround(rate * 0.040)) & ~1;
	hop = window / 2;
	search = std::max(8, (int)std::lround(rate * 0.012));
	hann.resize((size_t)window);
	for (int i = 0; i < window; i++)
		hann[(size_t)i] = (float)(0.5 - 0.5 * std::cos(2.0 * kPi * i / window));
	reset();
}

void TimeStretcher::reset()
{
	input.clear();
	inputStart = 0;
	inputEnd = 0;
	nominal = 0;
	previous = -1;
	accum.assign((size_t)window * (size_t)chans, 0.0f);
}

void TimeStretcher::push(const float *data, size_t frames, std::vector<float> &out)
{
	if (!configured() || !data || !frames)
		return;
	input.insert(input.end(), data, data + frames * (size_t)chans);
	inputEnd += (int64_t)frames;

	while (step(out)) {
	}

	/* drop input no window can reach any more */
	int64_t keepFrom = (int64_t)std::floor(nominal) - search;
	if (previous >= 0)
		keepFrom = std::min(keepFrom, previous + hop);
	if (keepFrom > inputStart) {
		const int64_t drop = std::min(keepFrom, inputEnd) - inputStart;
		input.erase(input.begin(), input.begin() + (ptrdiff_t)(drop * chans));
		inputStart += drop;
	}
}

float TimeStretcher::monoAt(int64_t frame) const
{
	const float *src = &input[(size_t)(frame - inputStart) * (size_t)chans];
	float sum = 0.0f;
	for (int c = 0; c < chans; c++)
		sum += src[c];
	return sum;
}

int64_t TimeStretcher::bestOffset(int64_t center) const
{
	/* the window should continue what the previous one was playing */
	const int64_t target = previous + hop;
	const int64_t lo = std::max(inputStart, center - search);
	const int64_t hi = std::min(center + search, inputEnd - window);
	if (hi < lo)
		return std::clamp(center, inputStart, std::max(inputStart, inputEnd - window));

	const int overlap = window - hop;
	const int stride = std::max(1, rate / 12000); /* compare at ~12 kHz */
	double bestScore = -std::numeric_limits<double>::infinity();
	int64_t best = std::clamp(center, lo, hi);
	for (int64_t pos = lo; pos <= hi; pos++) {
		double dot = 0;
		double energy = 0;
		for (int i = 0; i < overlap; i += stride) {
			const float a = monoAt(target + i);
			const float b = monoAt(pos + i);
			dot += (double)a * b;
			energy += (double)b * b;
		}
		const double score = energy > 0 ? dot / std::sqrt(energy) : 0;
		if (score > bestScore) {
			bestScore = score;
			best = pos;
		}
	}
	return best;
}

bool TimeStretcher::step(std::vector<float> &out)
{
	const int64_t center = (int64_t)std::llround(nominal);
	int64_t needed = (previous < 0 ? center : center + search) + window;
	if (previous >= 0)
		needed = std::max(needed, previous + hop + (window - hop));
	if (needed > inputEnd)
		return false;

	const int64_t pos = previous < 0 ? std::max(center, inputStart) : bestOffset(center);
	for (int i = 0; i < window; i++) {
		const float w = hann[(size_t)i];
		const float *src = &input[(size_t)(pos - inputStart + i) * (size_t)chans];
		float *dst = &accum[(size_t)i * (size_t)chans];
		for (int c = 0; c < chans; c++)
			dst[c] += w * src[c];
	}

	/* windows overlap by half, so the first hop of the buffer is final */
	const size_t done = (size_t)hop * (size_t)chans;
	out.insert(out.end(), accum.begin(), accum.begin() + (ptrdiff_t)done);
	std::move(accum.begin() + (ptrdiff_t)done, accum.end(), accum.begin());
	std::fill(accum.end() - (ptrdiff_t)done, accum.end(), 0.0f);

	previous = pos;
	nominal += hop * speed;
	return true;
}

} // namespace rc
