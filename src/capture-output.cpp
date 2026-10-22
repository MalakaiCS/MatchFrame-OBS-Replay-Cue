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

#include "capture-output.hpp"
#include "packet-buffer.hpp"
#include "rc-common.hpp"

#include <mutex>

namespace rc {

const char *kCaptureVideoOutputId = "replay_cue_capture_video";
const char *kCaptureAudioOutputId = "replay_cue_capture_audio";

namespace {

struct CaptureOutput {
	obs_output_t *output = nullptr;
	std::mutex mutex;
	PacketBuffer *buffer = nullptr;
};

void setBufferProc(void *data, calldata_t *cd)
{
	auto *ctx = static_cast<CaptureOutput *>(data);
	/* taking the lock guarantees no packet is mid-push once this returns */
	std::lock_guard<std::mutex> lock(ctx->mutex);
	ctx->buffer = static_cast<PacketBuffer *>(calldata_ptr(cd, "buffer"));
}

const char *videoName(void *)
{
	return "Replay Cue Video Capture";
}

const char *audioName(void *)
{
	return "Replay Cue Audio Capture";
}

void *create(obs_data_t *, obs_output_t *output)
{
	auto *ctx = new CaptureOutput();
	ctx->output = output;
	proc_handler_add(obs_output_get_proc_handler(output), "void set_buffer(ptr buffer)", setBufferProc, ctx);
	return ctx;
}

void destroy(void *data)
{
	delete static_cast<CaptureOutput *>(data);
}

bool start(void *data)
{
	auto *ctx = static_cast<CaptureOutput *>(data);
	if (!obs_output_can_begin_data_capture(ctx->output, 0))
		return false;
	if (!obs_output_initialize_encoders(ctx->output, 0))
		return false;
	return obs_output_begin_data_capture(ctx->output, 0);
}

void stop(void *data, uint64_t)
{
	auto *ctx = static_cast<CaptureOutput *>(data);
	obs_output_end_data_capture(ctx->output);
}

void encodedPacket(void *data, encoder_packet *packet)
{
	auto *ctx = static_cast<CaptureOutput *>(data);
	if (!packet)
		return;
	std::lock_guard<std::mutex> lock(ctx->mutex);
	if (ctx->buffer)
		ctx->buffer->push(packet);
}

} // namespace

void registerCaptureOutputs()
{
	obs_output_info videoInfo = {};
	videoInfo.id = kCaptureVideoOutputId;
	videoInfo.flags = OBS_OUTPUT_VIDEO | OBS_OUTPUT_ENCODED;
	videoInfo.get_name = videoName;
	videoInfo.create = create;
	videoInfo.destroy = destroy;
	videoInfo.start = start;
	videoInfo.stop = stop;
	videoInfo.encoded_packet = encodedPacket;
	obs_register_output(&videoInfo);

	obs_output_info audioInfo = videoInfo;
	audioInfo.id = kCaptureAudioOutputId;
	audioInfo.flags = OBS_OUTPUT_AUDIO | OBS_OUTPUT_ENCODED;
	audioInfo.get_name = audioName;
	obs_register_output(&audioInfo);
}

void setCaptureOutputBuffer(obs_output_t *output, PacketBuffer *buffer)
{
	if (!output)
		return;
	calldata_t cd;
	calldata_init(&cd);
	calldata_set_ptr(&cd, "buffer", buffer);
	proc_handler_call(obs_output_get_proc_handler(output), "set_buffer", &cd);
	calldata_free(&cd);
}

} // namespace rc
