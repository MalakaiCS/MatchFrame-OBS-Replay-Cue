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

#include "clean-feed.hpp"
#include "capture-engine.hpp"
#include "rc-common.hpp"

#include <algorithm>
#include <chrono>
#include <thread>

namespace rc {

struct CleanFeedRecorder::Session {
	CleanFeedSettings settings;
	obs_source_t *scene = nullptr; /* held, and marked as showing, while recording */
	obs_view_t *view = nullptr;
	video_t *video = nullptr;
	obs_encoder_t *videoEncoder = nullptr;
	obs_encoder_t *audioEncoder = nullptr;
	obs_output_t *output = nullptr;
	int64_t startedUsec = 0;

	~Session()
	{
		if (output) {
			if (obs_output_active(output))
				obs_output_force_stop(output);
			obs_output_release(output);
		}
		obs_encoder_release(videoEncoder);
		obs_encoder_release(audioEncoder);
		if (view) {
			obs_view_set_source(view, 0, nullptr);
			if (video)
				obs_view_remove(view);
			obs_view_destroy(view);
		}
		if (scene) {
			obs_source_dec_showing(scene);
			obs_source_release(scene);
		}
	}

	bool finished() const { return !output || !obs_output_active(output); }
};

CleanFeedRecorder::CleanFeedRecorder() = default;

CleanFeedRecorder::~CleanFeedRecorder()
{
	active.reset();
	finishing.clear();
}

bool CleanFeedRecorder::start(const CleanFeedSettings &settings, std::string &error)
{
	if (active)
		return true;

	auto session = std::make_unique<Session>();
	session->settings = settings;
	obs_source_t *scene = obs_get_source_by_name(settings.sceneName.c_str());
	if (!scene) {
		error = text("Msg.CleanFeedNoScene");
		return false;
	}
	/* drawn as a projector would: sources that only run while shown keep going */
	obs_source_inc_showing(scene);
	session->scene = scene;

	struct obs_video_info ovi = {};
	if (!obs_get_video_info(&ovi)) {
		error = text("Status.NoVideo");
		return false;
	}
	const bool canvasHdr = ovi.colorspace == VIDEO_CS_2100_PQ || ovi.colorspace == VIDEO_CS_2100_HLG;
	const std::string encoderId = CaptureEngine::resolveVideoEncoder(settings.encoderId, canvasHdr && settings.hdr);
	if (encoderId.empty()) {
		error = text("Status.NoEncoder");
		return false;
	}
	const bool useHdr = canvasHdr && settings.hdr && CaptureEngine::encoderSupportsHdr(encoderId);
	/* the canvas, scaled to the output size like OBS's own recording */
	ovi.output_format = useHdr ? VIDEO_FORMAT_P010 : VIDEO_FORMAT_NV12;
	if (canvasHdr && !useHdr)
		ovi.colorspace = VIDEO_CS_709;

	session->view = obs_view_create();
	obs_view_set_source(session->view, 0, scene);
	session->video = obs_view_add2(session->view, &ovi);
	if (!session->video) {
		error = text("Status.ViewFailed");
		return false;
	}

	CaptureSettings video;
	video.bitrateKbps = settings.bitrateKbps;
	video.keyintSec = 2;
	obs_data_t *videoSettings = makeVideoEncoderSettings(encoderId, video);
	session->videoEncoder =
		obs_video_encoder_create(encoderId.c_str(), "Replay Cue - Clean feed", videoSettings, nullptr);
	obs_data_release(videoSettings);
	if (!session->videoEncoder) {
		error = text("Status.EncoderFailed");
		return false;
	}
	obs_encoder_set_video(session->videoEncoder, session->video);

	const char *audioId = pickAudioEncoder();
	if (audioId && settings.audioTrack >= 1 && settings.audioTrack <= MAX_AUDIO_MIXES) {
		obs_data_t *audioSettings = obs_encoder_defaults(audioId);
		if (!audioSettings)
			audioSettings = obs_data_create();
		obs_data_set_int(audioSettings, "bitrate", 192);
		session->audioEncoder = obs_audio_encoder_create(audioId, "Replay Cue - Clean feed audio",
								 audioSettings, (size_t)(settings.audioTrack - 1),
								 nullptr);
		obs_data_release(audioSettings);
		if (session->audioEncoder)
			obs_encoder_set_audio(session->audioEncoder, obs_get_audio());
	}

	os_mkdirs(dirNameOf(settings.path).c_str());
	obs_data_t *outputSettings = obs_data_create();
	obs_data_set_string(outputSettings, "path", settings.path.c_str());
	/* a fragmented MP4 stays playable if OBS stops suddenly, like MKV */
	if (extensionOf(settings.path) == "mp4")
		obs_data_set_string(outputSettings, "muxer_settings", "movflags=frag_keyframe+empty_moov+delay_moov");
	session->output = obs_output_create("ffmpeg_muxer", "Replay Cue - Clean feed", outputSettings, nullptr);
	obs_data_release(outputSettings);
	if (!session->output) {
		error = text("Status.OutputFailed");
		return false;
	}
	obs_output_set_video_encoder(session->output, session->videoEncoder);
	if (session->audioEncoder)
		obs_output_set_audio_encoder(session->output, session->audioEncoder, 0);

	if (!obs_output_start(session->output)) {
		const char *err = obs_output_get_last_error(session->output);
		if (useHdr) {
			/* not every HEVC/AV1 encoder (or GPU) takes 10-bit */
			obs_log(LOG_WARNING, "clean feed: %s could not record HDR (%s), recording SDR instead",
				encoderId.c_str(), err ? err : "encoder refused 10-bit");
			session.reset();
			CleanFeedSettings sdr = settings;
			sdr.hdr = false;
			return start(sdr, error);
		}
		error = std::string(text("Status.EncoderFailed")) + (err ? std::string(": ") + err : std::string());
		return false;
	}

	session->startedUsec = nowUsec();
	obs_log(LOG_INFO, "clean feed: recording '%s' %ux%u with %s%s to %s", settings.sceneName.c_str(),
		ovi.output_width, ovi.output_height, encoderId.c_str(), useHdr ? " in HDR" : "", settings.path.c_str());
	active = std::move(session);
	return true;
}

void CleanFeedRecorder::stop()
{
	if (!active)
		return;
	if (active->output && obs_output_active(active->output))
		obs_output_stop(active->output);
	obs_log(LOG_INFO, "clean feed: stopped %s", active->settings.path.c_str());
	finishing.push_back(std::move(active));
}

bool CleanFeedRecorder::tick(std::string &error)
{
	finishing.erase(std::remove_if(finishing.begin(), finishing.end(),
				       [](const std::unique_ptr<Session> &s) { return s->finished(); }),
			finishing.end());

	if (!active || !active->finished())
		return false;
	/* the output stopped by itself: the disk is full, the encoder failed... */
	const char *err = obs_output_get_last_error(active->output);
	error = err && *err ? err : text("Msg.CleanFeedInterrupted");
	obs_log(LOG_WARNING, "clean feed: recording of %s stopped: %s", active->settings.path.c_str(), error.c_str());
	active.reset();
	return true;
}

void CleanFeedRecorder::shutdown()
{
	stop();
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (!finishing.empty() && std::chrono::steady_clock::now() < deadline) {
		std::string ignored;
		tick(ignored);
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
	finishing.clear();
}

std::string CleanFeedRecorder::file() const
{
	return active ? active->settings.path : std::string();
}

std::string CleanFeedRecorder::scene() const
{
	return active ? active->settings.sceneName : std::string();
}

double CleanFeedRecorder::seconds() const
{
	return active ? (double)(nowUsec() - active->startedUsec) / 1e6 : 0.0;
}

} // namespace rc
