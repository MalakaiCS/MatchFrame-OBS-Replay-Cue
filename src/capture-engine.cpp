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

#include "capture-engine.hpp"
#include "capture-output.hpp"
#include "rc-common.hpp"

#include <media-io/audio-io.h>
#include <obs.hpp>

#include <algorithm>
#include <cstring>
#include <limits>

namespace rc {

namespace {

constexpr int64_t kRetryDelayUsec = 3 * 1000000LL;

bool encoderExists(const char *id)
{
	return id && *id && obs_get_encoder_codec(id) != nullptr;
}

bool isUsableVideoEncoder(const char *id)
{
	if (!encoderExists(id) || obs_get_encoder_type(id) != OBS_ENCODER_VIDEO)
		return false;
	uint32_t caps = obs_get_encoder_caps(id);
	if (caps & (OBS_ENCODER_CAP_DEPRECATED | OBS_ENCODER_CAP_INTERNAL))
		return false;
	const char *codec = obs_get_encoder_codec(id);
	return strcmp(codec, "h264") == 0 || strcmp(codec, "hevc") == 0 || strcmp(codec, "av1") == 0;
}

obs_data_t *makeVideoEncoderSettings(const std::string &id, const CaptureSettings &s)
{
	obs_data_t *settings = obs_encoder_defaults(id.c_str());
	if (!settings)
		settings = obs_data_create();

	obs_data_set_string(settings, "rate_control", "CBR");
	obs_data_set_int(settings, "bitrate", s.bitrateKbps);
	obs_data_set_int(settings, "keyint_sec", std::max(1, s.keyintSec));

	/* No B-frames: decode order equals display order, so every cut is
	 * frame-exact and the player can scrub backwards cheaply. */
	if (id.find("videotoolbox") != std::string::npos) {
		obs_data_set_bool(settings, "bframes", false);
	} else if (id == "obs_x264") {
		obs_data_set_string(settings, "x264opts", "bframes=0");
	} else {
		obs_data_set_int(settings, "bf", 0);
		obs_data_set_int(settings, "bframes", 0);
	}
	return settings;
}

const char *pickAudioEncoder()
{
	static const char *candidates[] = {"CoreAudio_AAC", "libfdk_aac", "ffmpeg_aac", "ffmpeg_opus"};
	for (const char *id : candidates)
		if (encoderExists(id))
			return id;
	return nullptr;
}

} // namespace

/* ------------------------------------------------------------------------- */

AngleCapture::AngleCapture(AngleSpec spec, const CaptureSettings &s) : angle(std::move(spec)), settings(s)
{
	state = text("Status.Waiting");
}

AngleCapture::~AngleCapture()
{
	stop();
}

void AngleCapture::fail(const std::string &why)
{
	obs_log(LOG_WARNING, "[%s] %s", angle.label.c_str(), why.c_str());
	state = why;
	stop();
	state = why;
	retryAfterUsec = nowUsec() + kRetryDelayUsec;
}

bool AngleCapture::start(obs_source_t *source)
{
	uint32_t width = obs_source_get_width(source);
	uint32_t height = obs_source_get_height(source);
	width += width & 1;
	height += height & 1;
	if (!width || !height) {
		state = text("Status.NoVideo");
		return false;
	}

	struct obs_video_info ovi = {};
	if (!obs_get_video_info(&ovi)) {
		state = text("Status.NoVideo");
		return false;
	}

	uint32_t outW = width;
	uint32_t outH = height;
	if (settings.maxHeight > 0 && height > (uint32_t)settings.maxHeight) {
		outH = (uint32_t)settings.maxHeight & ~1u;
		outW = (uint32_t)((uint64_t)width * outH / height);
		outW += outW & 1;
	}

	ovi.base_width = width;
	ovi.base_height = height;
	ovi.output_width = outW;
	ovi.output_height = outH;
	ovi.output_format = VIDEO_FORMAT_NV12;
	ovi.scale_type = OBS_SCALE_BICUBIC;
	if (ovi.colorspace == VIDEO_CS_2100_PQ || ovi.colorspace == VIDEO_CS_2100_HLG)
		ovi.colorspace = VIDEO_CS_709;

	view = obs_view_create();
	obs_view_set_source(view, 0, source);
	video = obs_view_add2(view, &ovi);
	if (!video) {
		fail(text("Status.ViewFailed"));
		return false;
	}

	const std::string encoderId = CaptureEngine::resolveVideoEncoder(settings.encoderId);
	if (encoderId.empty()) {
		fail(text("Status.NoEncoder"));
		return false;
	}

	packets = std::make_shared<PacketBuffer>(true);
	packets->setMaxDurationUsec((int64_t)settings.bufferSeconds * 1000000LL);
	StreamInfo info;
	info.video = true;
	info.codec = obs_get_encoder_codec(encoderId.c_str());
	info.width = outW;
	info.height = outH;
	info.fpsNum = ovi.fps_num;
	info.fpsDen = ovi.fps_den;
	packets->setStreamInfo(info);

	const std::string name = "Replay Cue - " + angle.label;
	obs_data_t *encSettings = makeVideoEncoderSettings(encoderId, settings);
	encoder = obs_video_encoder_create(encoderId.c_str(), name.c_str(), encSettings, nullptr);
	obs_data_release(encSettings);
	if (!encoder) {
		fail(text("Status.EncoderFailed"));
		return false;
	}
	obs_encoder_set_video(encoder, video);

	output = obs_output_create(kCaptureVideoOutputId, name.c_str(), nullptr, nullptr);
	if (!output) {
		fail(text("Status.OutputFailed"));
		return false;
	}
	obs_output_set_video_encoder(output, encoder);
	setCaptureOutputBuffer(output, packets.get());

	if (!obs_output_start(output)) {
		const char *err = obs_output_get_last_error(output);
		fail(std::string(text("Status.EncoderFailed")) + (err ? std::string(": ") + err : std::string()));
		return false;
	}

	baseWidth = width;
	baseHeight = height;
	outWidth = outW;
	outHeight = outH;
	state = text("Status.Recording");
	obs_log(LOG_INFO, "[%s] capturing '%s' %ux%u -> %ux%u with %s", angle.label.c_str(), angle.sourceName.c_str(),
		width, height, outW, outH, encoderId.c_str());
	return true;
}

void AngleCapture::stop()
{
	if (output) {
		/* detach first: once this returns no encoder thread is inside push() */
		setCaptureOutputBuffer(output, nullptr);
		obs_output_force_stop(output);
		obs_output_release(output);
		output = nullptr;
	}
	if (encoder) {
		obs_encoder_release(encoder);
		encoder = nullptr;
	}
	if (view) {
		obs_view_set_source(view, 0, nullptr);
		if (video)
			obs_view_remove(view);
		obs_view_destroy(view);
		view = nullptr;
	}
	video = nullptr;
	baseWidth = baseHeight = outWidth = outHeight = 0;
	state = text("Status.Stopped");
}

void AngleCapture::tick()
{
	OBSSourceAutoRelease source = obs_get_source_by_name(angle.sourceName.c_str());
	if (!source) {
		if (output)
			stop();
		state = text("Status.SourceMissing");
		return;
	}

	if (!output) {
		if (nowUsec() < retryAfterUsec)
			return;
		if (!start(source))
			retryAfterUsec = std::max(retryAfterUsec, nowUsec() + kRetryDelayUsec);
		return;
	}

	uint32_t width = obs_source_get_width(source);
	uint32_t height = obs_source_get_height(source);
	width += width & 1;
	height += height & 1;
	bool resized = width && height && (width != baseWidth || height != baseHeight);
	bool died = !obs_output_active(output);

	if (resized || died) {
		obs_log(LOG_INFO, "[%s] restarting capture (%s)", angle.label.c_str(),
			resized ? "source size changed" : "encoder stopped");
		stop();
		start(source);
	}
}

AngleStatus AngleCapture::status() const
{
	AngleStatus st;
	st.label = angle.label;
	st.sourceName = angle.sourceName;
	st.running = output != nullptr;
	st.state = state;
	st.width = outWidth;
	st.height = outHeight;
	if (packets && !packets->empty()) {
		st.bufferedSeconds = (double)(packets->newestUsec() - packets->oldestUsec()) / 1e6;
		st.bytes = packets->byteSize();
	}
	return st;
}

/* ------------------------------------------------------------------------- */

AudioCapture::AudioCapture(int track, int bufferSeconds)
{
	const char *id = pickAudioEncoder();
	if (!id || track < 1 || track > MAX_AUDIO_MIXES) {
		obs_log(LOG_WARNING, "no audio encoder available, replays will be silent");
		return;
	}

	obs_data_t *settings = obs_encoder_defaults(id);
	if (!settings)
		settings = obs_data_create();
	obs_data_set_int(settings, "bitrate", 192);
	encoder = obs_audio_encoder_create(id, "Replay Cue - Audio", settings, (size_t)(track - 1), nullptr);
	obs_data_release(settings);
	if (!encoder)
		return;
	obs_encoder_set_audio(encoder, obs_get_audio());

	packets = std::make_shared<PacketBuffer>(false);
	packets->setMaxDurationUsec((int64_t)bufferSeconds * 1000000LL);
	StreamInfo info;
	info.video = false;
	info.codec = obs_encoder_get_codec(encoder);
	info.sampleRate = obs_encoder_get_sample_rate(encoder);
	info.channels = (uint32_t)audio_output_get_channels(obs_get_audio());
	packets->setStreamInfo(info);

	output = obs_output_create(kCaptureAudioOutputId, "Replay Cue - Audio", nullptr, nullptr);
	if (!output)
		return;
	obs_output_set_audio_encoder(output, encoder, 0);
	setCaptureOutputBuffer(output, packets.get());
	if (!obs_output_start(output)) {
		const char *err = obs_output_get_last_error(output);
		obs_log(LOG_WARNING, "audio capture failed to start: %s", err ? err : "unknown error");
		setCaptureOutputBuffer(output, nullptr);
		obs_output_release(output);
		output = nullptr;
		packets.reset();
		return;
	}
	obs_log(LOG_INFO, "capturing audio track %d with %s", track, id);
}

AudioCapture::~AudioCapture()
{
	if (output) {
		setCaptureOutputBuffer(output, nullptr);
		obs_output_force_stop(output);
		obs_output_release(output);
	}
	if (encoder)
		obs_encoder_release(encoder);
}

/* ------------------------------------------------------------------------- */

CaptureEngine::~CaptureEngine()
{
	disarm();
}

void CaptureEngine::arm(const std::vector<AngleSpec> &specs, const CaptureSettings &settings)
{
	disarm();
	current = settings;
	for (const AngleSpec &spec : specs) {
		if (spec.sourceName.empty() || angles.size() >= (size_t)kMaxAngles)
			continue;
		angles.push_back(std::make_unique<AngleCapture>(spec, settings));
	}
	if (settings.audioTrack > 0) {
		audio = std::make_unique<AudioCapture>(settings.audioTrack, settings.bufferSeconds);
		if (!audio->running())
			audio.reset();
	}
	isArmed = true;
	tick();
}

void CaptureEngine::disarm()
{
	angles.clear();
	audio.reset();
	isArmed = false;
}

void CaptureEngine::tick()
{
	for (auto &angle : angles)
		angle->tick();
}

std::vector<AngleStatus> CaptureEngine::status() const
{
	std::vector<AngleStatus> out;
	for (const auto &angle : angles)
		out.push_back(angle->status());
	return out;
}

std::vector<CaptureEngine::BufferRef> CaptureEngine::videoBuffers() const
{
	std::vector<BufferRef> out;
	for (const auto &angle : angles) {
		auto buffer = angle->buffer();
		if (buffer && !buffer->empty())
			out.push_back({angle->spec().label, buffer});
	}
	return out;
}

std::shared_ptr<PacketBuffer> CaptureEngine::audioBuffer() const
{
	return audio ? audio->buffer() : nullptr;
}

int64_t CaptureEngine::oldestCommonUsec() const
{
	int64_t oldest = std::numeric_limits<int64_t>::min();
	for (const auto &angle : angles) {
		auto buffer = angle->buffer();
		if (buffer && !buffer->empty())
			oldest = std::max(oldest, buffer->oldestUsec());
	}
	return oldest;
}

std::vector<std::pair<std::string, std::string>> CaptureEngine::availableVideoEncoders()
{
	std::vector<std::pair<std::string, std::string>> out;
	const char *id = nullptr;
	for (size_t i = 0; obs_enum_encoder_types(i, &id); i++) {
		if (!isUsableVideoEncoder(id))
			continue;
		const char *name = obs_encoder_get_display_name(id);
		out.emplace_back(id, name ? name : id);
	}
	return out;
}

std::string CaptureEngine::defaultVideoEncoder()
{
	/* hardware first: several cameras at once is too much for x264 */
	static const char *preferred[] = {
		"obs_nvenc_h264_tex",
		"jim_nvenc",
		"com.apple.videotoolbox.videoencoder.ave.avc",
		"com.apple.videotoolbox.videoencoder.h264.gva",
		"obs_qsv11_v2",
		"obs_qsv11",
		"h264_texture_amf",
		"obs_nvenc_h264_soft",
		"obs_x264",
	};
	for (const char *id : preferred)
		if (isUsableVideoEncoder(id))
			return id;
	auto all = availableVideoEncoders();
	return all.empty() ? std::string() : all.front().first;
}

std::string CaptureEngine::resolveVideoEncoder(const std::string &preferred)
{
	if (!preferred.empty() && isUsableVideoEncoder(preferred.c_str()))
		return preferred;
	return defaultVideoEncoder();
}

} // namespace rc
