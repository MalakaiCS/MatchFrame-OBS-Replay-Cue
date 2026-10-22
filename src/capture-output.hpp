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

namespace rc {

class PacketBuffer;

/* Encoded-only outputs that hand every packet to a PacketBuffer instead of
 * writing to disk or the network. One for video angles, one for audio. */
extern const char *kCaptureVideoOutputId;
extern const char *kCaptureAudioOutputId;

void registerCaptureOutputs();

/* Points a capture output at the buffer it should fill (nullptr to detach).
 * The buffer must outlive the output's data capture. */
void setCaptureOutputBuffer(obs_output_t *output, PacketBuffer *buffer);

} // namespace rc
