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

#include "replay-player.hpp"
#include "ffmpeg-compat.hpp"
#include "rc-common.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>

namespace rc {

const char *kPlayerSourceId = "replay_cue_player";

const char *playerStateName(PlayerState state)
{
	switch (state) {
	case PlayerState::Idle:
		return "idle";
	case PlayerState::Loading:
		return "loading";
	case PlayerState::Playing:
		return "playing";
	case PlayerState::Paused:
		return "paused";
	case PlayerState::Ended:
		return "ended";
	case PlayerState::Error:
		return "error";
	}
	return "idle";
}

namespace {

constexpr size_t kMaxQueuedFrames = 12;
constexpr int64_t kPrerollTimeoutUsec = 750000;

enum video_format convertPixelFormat(int format)
{
	switch (format) {
	case AV_PIX_FMT_YUV420P:
	case AV_PIX_FMT_YUVJ420P:
		return VIDEO_FORMAT_I420;
	case AV_PIX_FMT_NV12:
		return VIDEO_FORMAT_NV12;
	case AV_PIX_FMT_YUYV422:
		return VIDEO_FORMAT_YUY2;
	case AV_PIX_FMT_UYVY422:
		return VIDEO_FORMAT_UYVY;
	case AV_PIX_FMT_YVYU422:
		return VIDEO_FORMAT_YVYU;
	case AV_PIX_FMT_YUV422P:
	case AV_PIX_FMT_YUVJ422P:
		return VIDEO_FORMAT_I422;
	case AV_PIX_FMT_YUV422P10LE:
		return VIDEO_FORMAT_I210;
	case AV_PIX_FMT_YUV444P:
	case AV_PIX_FMT_YUVJ444P:
		return VIDEO_FORMAT_I444;
	case AV_PIX_FMT_YUV444P12LE:
		return VIDEO_FORMAT_I412;
	case AV_PIX_FMT_RGBA:
		return VIDEO_FORMAT_RGBA;
	case AV_PIX_FMT_BGRA:
		return VIDEO_FORMAT_BGRA;
	case AV_PIX_FMT_BGR0:
		return VIDEO_FORMAT_BGRX;
	case AV_PIX_FMT_YUVA420P:
		return VIDEO_FORMAT_I40A;
	case AV_PIX_FMT_YUVA422P:
		return VIDEO_FORMAT_I42A;
	case AV_PIX_FMT_YUVA444P:
		return VIDEO_FORMAT_YUVA;
	case AV_PIX_FMT_YUV420P10LE:
		return VIDEO_FORMAT_I010;
	case AV_PIX_FMT_P010LE:
		return VIDEO_FORMAT_P010;
	default:
		return VIDEO_FORMAT_NONE;
	}
}

enum audio_format convertSampleFormat(int format)
{
	switch (format) {
	case AV_SAMPLE_FMT_U8:
		return AUDIO_FORMAT_U8BIT;
	case AV_SAMPLE_FMT_S16:
		return AUDIO_FORMAT_16BIT;
	case AV_SAMPLE_FMT_S32:
		return AUDIO_FORMAT_32BIT;
	case AV_SAMPLE_FMT_FLT:
		return AUDIO_FORMAT_FLOAT;
	case AV_SAMPLE_FMT_U8P:
		return AUDIO_FORMAT_U8BIT_PLANAR;
	case AV_SAMPLE_FMT_S16P:
		return AUDIO_FORMAT_16BIT_PLANAR;
	case AV_SAMPLE_FMT_S32P:
		return AUDIO_FORMAT_32BIT_PLANAR;
	case AV_SAMPLE_FMT_FLTP:
		return AUDIO_FORMAT_FLOAT_PLANAR;
	default:
		return AUDIO_FORMAT_UNKNOWN;
	}
}

enum speaker_layout convertSpeakers(int channels)
{
	switch (channels) {
	case 1:
		return SPEAKERS_MONO;
	case 2:
		return SPEAKERS_STEREO;
	case 3:
		return SPEAKERS_2POINT1;
	case 4:
		return SPEAKERS_4POINT0;
	case 5:
		return SPEAKERS_4POINT1;
	case 6:
		return SPEAKERS_5POINT1;
	case 8:
		return SPEAKERS_7POINT1;
	default:
		return SPEAKERS_UNKNOWN;
	}
}

enum video_colorspace convertColorSpace(enum AVColorSpace space, enum AVColorTransferCharacteristic trc,
					enum AVColorPrimaries primaries)
{
	switch (space) {
	case AVCOL_SPC_BT709:
		return trc == AVCOL_TRC_IEC61966_2_1 ? VIDEO_CS_SRGB : VIDEO_CS_709;
	case AVCOL_SPC_FCC:
	case AVCOL_SPC_BT470BG:
	case AVCOL_SPC_SMPTE170M:
	case AVCOL_SPC_SMPTE240M:
		return VIDEO_CS_601;
	case AVCOL_SPC_BT2020_NCL:
		return trc == AVCOL_TRC_ARIB_STD_B67 ? VIDEO_CS_2100_HLG : VIDEO_CS_2100_PQ;
	default:
		if (primaries == AVCOL_PRI_BT2020)
			return trc == AVCOL_TRC_ARIB_STD_B67 ? VIDEO_CS_2100_HLG : VIDEO_CS_2100_PQ;
		return VIDEO_CS_DEFAULT;
	}
}

enum video_trc convertTrc(enum AVColorTransferCharacteristic trc)
{
	switch (trc) {
	case AVCOL_TRC_SMPTE2084:
		return VIDEO_TRC_PQ;
	case AVCOL_TRC_ARIB_STD_B67:
		return VIDEO_TRC_HLG;
	case AVCOL_TRC_IEC61966_2_1:
		return VIDEO_TRC_SRGB;
	default:
		return VIDEO_TRC_DEFAULT;
	}
}

enum AVHWDeviceType preferredHwDevice()
{
#if defined(__APPLE__)
	return AV_HWDEVICE_TYPE_VIDEOTOOLBOX;
#elif defined(_WIN32)
	return AV_HWDEVICE_TYPE_D3D11VA;
#else
	return AV_HWDEVICE_TYPE_VAAPI;
#endif
}

struct QueuedFrame {
	AVFrame *frame = nullptr;
	double time = 0;
	uint64_t gen = 0;
};

void freeQueue(std::deque<QueuedFrame> &queue)
{
	for (QueuedFrame &q : queue)
		av_frame_free(&q.frame);
	queue.clear();
}

} // namespace

/* ------------------------------------------------------------------------- */
/* One file, decoded on its own thread into a small queue of frames.         */

class AngleDecoder {
public:
	AngleDecoder(PlayAngle angle_, bool hw_, bool audio_) : angle(std::move(angle_)), useHw(hw_), wantAudio(audio_)
	{
	}

	~AngleDecoder()
	{
		{
			std::lock_guard<std::mutex> lock(mutex);
			quit = true;
		}
		cv.notify_all();
		if (thread.joinable())
			thread.join();
		freeQueue(video);
		freeQueue(audio);
		if (sws)
			sws_freeContext(sws);
		avcodec_free_context(&vctx);
		avcodec_free_context(&actx);
		av_buffer_unref(&hwDevice);
		avformat_close_input(&fmt);
	}

	bool open(std::string &error)
	{
		if (!ffmpegReady()) {
			error = "FFmpeg libraries are not compatible with this OBS version";
			return false;
		}

		int ret = avformat_open_input(&fmt, angle.file.c_str(), nullptr, nullptr);
		if (ret < 0) {
			error = "could not open " + angle.file + ": " + averr(ret);
			return false;
		}
		if ((ret = avformat_find_stream_info(fmt, nullptr)) < 0) {
			error = "could not read " + angle.file + ": " + averr(ret);
			return false;
		}

		const AVCodec *vcodec = nullptr;
		vidx = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &vcodec, 0);
		if (vidx < 0 || !vcodec) {
			error = angle.file + " has no playable video";
			return false;
		}

		AVStream *vs = fmt->streams[vidx];
		vtb = vs->time_base;
		originSec = vs->start_time != AV_NOPTS_VALUE ? vs->start_time * av_q2d(vtb) : 0.0;
		AVRational rate = vs->avg_frame_rate.num ? vs->avg_frame_rate : vs->r_frame_rate;
		frameDur = rate.num > 0 && rate.den > 0 ? av_q2d(av_inv_q(rate)) : 1.0 / 30.0;
		if (fmt->duration != AV_NOPTS_VALUE && fmt->duration > 0)
			fileDuration = (double)fmt->duration / AV_TIME_BASE;
		else if (vs->duration != AV_NOPTS_VALUE)
			fileDuration = vs->duration * av_q2d(vtb);

		if (!openVideoDecoder(vcodec, vs, useHw) && !(useHw && openVideoDecoder(vcodec, vs, false))) {
			error = "could not open the video decoder for " + angle.file;
			return false;
		}

		if (wantAudio) {
			const AVCodec *acodec = nullptr;
			aidx = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, vidx, &acodec, 0);
			if (aidx >= 0 && acodec) {
				actx = avcodec_alloc_context3(acodec);
				if (!actx || avcodec_parameters_to_context(actx, fmt->streams[aidx]->codecpar) < 0 ||
				    avcodec_open2(actx, acodec, nullptr) < 0) {
					avcodec_free_context(&actx);
					aidx = -1;
				} else {
					atb = fmt->streams[aidx]->time_base;
					actx->pkt_timebase = atb;
				}
			} else {
				aidx = -1;
			}
		}

		for (unsigned i = 0; i < fmt->nb_streams; i++)
			if ((int)i != vidx && (int)i != aidx)
				fmt->streams[i]->discard = AVDISCARD_ALL;

		ok = true;
		return true;
	}

	void start()
	{
		if (ok)
			thread = std::thread([this]() { run(); });
	}

	void seek(double fileTime, uint64_t gen)
	{
		{
			std::lock_guard<std::mutex> lock(mutex);
			seekPending = true;
			seekTarget = fileTime;
			pendingGen = gen;
		}
		cv.notify_all();
	}

	/* Returns the newest frame at or before `time` (caller frees), dropping
	 * older ones. */
	AVFrame *popVideoUntil(double time, uint64_t gen)
	{
		AVFrame *result = nullptr;
		bool popped = false;
		{
			std::lock_guard<std::mutex> lock(mutex);
			while (!video.empty()) {
				QueuedFrame &q = video.front();
				if (q.gen == gen && q.time > time)
					break;
				if (q.gen == gen) {
					av_frame_free(&result);
					result = q.frame;
				} else {
					av_frame_free(&q.frame);
				}
				video.pop_front();
				popped = true;
			}
		}
		if (popped)
			cv.notify_all();
		return result;
	}

	bool hasVideo(uint64_t gen)
	{
		std::lock_guard<std::mutex> lock(mutex);
		for (const QueuedFrame &q : video)
			if (q.gen == gen)
				return true;
		return eof && !seekPending;
	}

	void popAudioUntil(double time, uint64_t gen, std::vector<QueuedFrame> *out)
	{
		std::lock_guard<std::mutex> lock(mutex);
		while (!audio.empty()) {
			QueuedFrame &q = audio.front();
			if (q.gen == gen && q.time > time)
				break;
			if (q.gen == gen && out)
				out->push_back(q);
			else
				av_frame_free(&q.frame);
			audio.pop_front();
		}
	}

	bool isOk() const { return ok; }
	const PlayAngle &info() const { return angle; }
	double duration() const { return fileDuration; }
	double frameDuration() const { return frameDur; }

private:
	static enum AVPixelFormat pickHwFormat(AVCodecContext *ctx, const enum AVPixelFormat *formats)
	{
		auto *self = static_cast<AngleDecoder *>(ctx->opaque);
		for (const enum AVPixelFormat *p = formats; *p != AV_PIX_FMT_NONE; p++)
			if (*p == self->hwPixFmt)
				return *p;
		/* the hardware cannot take this stream, fall back to software */
		for (const enum AVPixelFormat *p = formats; *p != AV_PIX_FMT_NONE; p++) {
			const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(*p);
			if (desc && !(desc->flags & AV_PIX_FMT_FLAG_HWACCEL))
				return *p;
		}
		return AV_PIX_FMT_NONE;
	}

	bool openVideoDecoder(const AVCodec *codec, AVStream *vs, bool hw)
	{
		avcodec_free_context(&vctx);
		av_buffer_unref(&hwDevice);
		hwPixFmt = AV_PIX_FMT_NONE;

		vctx = avcodec_alloc_context3(codec);
		if (!vctx || avcodec_parameters_to_context(vctx, vs->codecpar) < 0)
			return false;
		vctx->pkt_timebase = vs->time_base;
		vctx->opaque = this;

		if (hw) {
			const enum AVHWDeviceType type = preferredHwDevice();
			for (int i = 0;; i++) {
				const AVCodecHWConfig *config = avcodec_get_hw_config(codec, i);
				if (!config)
					break;
				if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) &&
				    config->device_type == type) {
					hwPixFmt = config->pix_fmt;
					break;
				}
			}
			if (hwPixFmt == AV_PIX_FMT_NONE ||
			    av_hwdevice_ctx_create(&hwDevice, type, nullptr, nullptr, 0) < 0)
				return false;
			vctx->hw_device_ctx = av_buffer_ref(hwDevice);
			vctx->get_format = pickHwFormat;
		} else {
			vctx->thread_count = 0;
			vctx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
		}

		return avcodec_open2(vctx, codec, nullptr) >= 0;
	}

	double frameTime(const AVFrame *frame, AVRational tb, double &last, double step)
	{
		int64_t ts = frame->best_effort_timestamp != AV_NOPTS_VALUE ? frame->best_effort_timestamp : frame->pts;
		double t = ts != AV_NOPTS_VALUE ? ts * av_q2d(tb) - originSec : last + step;
		last = t;
		return t;
	}

	AVFrame *toObsCompatible(AVFrame *frame)
	{
		if (convertPixelFormat(frame->format) != VIDEO_FORMAT_NONE)
			return frame;

		const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get((enum AVPixelFormat)frame->format);
		const enum AVPixelFormat target = desc && desc->comp[0].depth > 8 ? AV_PIX_FMT_YUV420P10LE
										  : AV_PIX_FMT_YUV420P;
		sws = sws_getCachedContext(sws, frame->width, frame->height, (enum AVPixelFormat)frame->format,
					   frame->width, frame->height, target, SWS_BILINEAR, nullptr, nullptr,
					   nullptr);
		AVFrame *converted = av_frame_alloc();
		if (!sws || !converted) {
			av_frame_free(&converted);
			return frame;
		}
		converted->format = target;
		converted->width = frame->width;
		converted->height = frame->height;
		if (av_frame_get_buffer(converted, 0) < 0) {
			av_frame_free(&converted);
			return frame;
		}
		sws_scale(sws, frame->data, frame->linesize, 0, frame->height, converted->data, converted->linesize);
		av_frame_copy_props(converted, frame);
		av_frame_free(&frame);
		return converted;
	}

	void pushVideo(AVFrame *decoded)
	{
		double t = frameTime(decoded, vtb, lastVideoTime, frameDur);
		if (t < skipBefore) {
			av_frame_unref(decoded);
			return;
		}

		AVFrame *frame = av_frame_alloc();
		if (hwPixFmt != AV_PIX_FMT_NONE && decoded->format == hwPixFmt) {
			if (av_hwframe_transfer_data(frame, decoded, 0) < 0) {
				av_frame_free(&frame);
				av_frame_unref(decoded);
				return;
			}
			av_frame_copy_props(frame, decoded);
			av_frame_unref(decoded);
		} else {
			av_frame_move_ref(frame, decoded);
		}
		frame = toObsCompatible(frame);

		std::lock_guard<std::mutex> lock(mutex);
		if (seekPending || quit) {
			av_frame_free(&frame);
			return;
		}
		video.push_back({frame, t, gen});
	}

	void pushAudio(AVFrame *decoded)
	{
		double step = decoded->sample_rate > 0 ? (double)decoded->nb_samples / decoded->sample_rate : 0.02;
		double t = frameTime(decoded, atb, lastAudioTime, step);
		if (t < skipBefore - 0.1) {
			av_frame_unref(decoded);
			return;
		}
		AVFrame *frame = av_frame_alloc();
		av_frame_move_ref(frame, decoded);

		std::lock_guard<std::mutex> lock(mutex);
		if (seekPending || quit) {
			av_frame_free(&frame);
			return;
		}
		audio.push_back({frame, t, gen});
	}

	void performSeek(double target)
	{
		int64_t ts = (int64_t)std::llround((target + originSec) / av_q2d(vtb));
		if (target <= 0)
			ts = (int64_t)std::llround(originSec / av_q2d(vtb));
		if (av_seek_frame(fmt, vidx, ts, AVSEEK_FLAG_BACKWARD) < 0)
			avformat_seek_file(fmt, vidx, INT64_MIN, ts, ts, 0);
		avcodec_flush_buffers(vctx);
		if (actx)
			avcodec_flush_buffers(actx);
		skipBefore = target - frameDur * 0.5;
		lastVideoTime = target;
		lastAudioTime = target;
	}

	/* returns the last receive code: 0, AVERROR(EAGAIN) or AVERROR_EOF */
	int drain(AVCodecContext *ctx, AVFrame *frame, bool isVideo)
	{
		int ret;
		while ((ret = avcodec_receive_frame(ctx, frame)) == 0) {
			if (isVideo)
				pushVideo(frame);
			else
				pushAudio(frame);
		}
		return ret;
	}

	void send(AVCodecContext *ctx, AVPacket *pkt, AVFrame *frame, bool isVideo)
	{
		int ret = avcodec_send_packet(ctx, pkt);
		while (ret == AVERROR(EAGAIN)) {
			drain(ctx, frame, isVideo);
			ret = avcodec_send_packet(ctx, pkt);
		}
	}

	void run()
	{
		AVPacket *pkt = av_packet_alloc();
		AVFrame *frame = av_frame_alloc();
		bool flushing = false;

		for (;;) {
			bool doSeek = false;
			double target = 0;
			{
				std::unique_lock<std::mutex> lock(mutex);
				cv.wait(lock, [this]() {
					return quit || seekPending || (!eof && video.size() < kMaxQueuedFrames);
				});
				if (quit)
					break;
				if (seekPending) {
					seekPending = false;
					gen = pendingGen;
					target = seekTarget;
					freeQueue(video);
					freeQueue(audio);
					eof = false;
					doSeek = true;
				}
			}

			if (doSeek) {
				performSeek(target);
				flushing = false;
				continue;
			}

			if (!flushing) {
				int ret = av_read_frame(fmt, pkt);
				if (ret < 0) {
					flushing = true;
					send(vctx, nullptr, frame, true);
					if (actx)
						send(actx, nullptr, frame, false);
				} else {
					if (pkt->stream_index == vidx)
						send(vctx, pkt, frame, true);
					else if (pkt->stream_index == aidx && actx)
						send(actx, pkt, frame, false);
					av_packet_unref(pkt);
				}
			}

			int videoRet = drain(vctx, frame, true);
			if (actx)
				drain(actx, frame, false);

			if (flushing && videoRet == AVERROR_EOF) {
				std::lock_guard<std::mutex> lock(mutex);
				eof = true;
			}
		}

		av_frame_free(&frame);
		av_packet_free(&pkt);
	}

	PlayAngle angle;
	bool useHw;
	bool wantAudio;
	bool ok = false;

	AVFormatContext *fmt = nullptr;
	AVCodecContext *vctx = nullptr;
	AVCodecContext *actx = nullptr;
	AVBufferRef *hwDevice = nullptr;
	enum AVPixelFormat hwPixFmt = AV_PIX_FMT_NONE;
	SwsContext *sws = nullptr;
	int vidx = -1;
	int aidx = -1;
	AVRational vtb = {1, 1};
	AVRational atb = {1, 1};
	double originSec = 0;
	double frameDur = 1.0 / 30.0;
	double fileDuration = 0;

	/* decoder thread only */
	double skipBefore = -1e9;
	double lastVideoTime = 0;
	double lastAudioTime = 0;

	std::thread thread;
	std::mutex mutex;
	std::condition_variable cv;
	std::deque<QueuedFrame> video;
	std::deque<QueuedFrame> audio;
	bool quit = false;
	bool eof = false;
	bool seekPending = false;
	double seekTarget = 0;
	uint64_t pendingGen = 0;
	uint64_t gen = 0;
};

/* ------------------------------------------------------------------------- */

ReplayPlayer::ReplayPlayer(obs_source_t *source) : obsSource(source)
{
	thread = std::thread([this]() { run(); });
}

ReplayPlayer::~ReplayPlayer()
{
	{
		std::lock_guard<std::mutex> lock(mutex);
		quit = true;
	}
	cv.notify_all();
	if (thread.joinable())
		thread.join();
	decoders.clear();
	for (AVFrame *&frame : current)
		av_frame_free(&frame);
}

void ReplayPlayer::load(const PlayRequest &request)
{
	{
		std::lock_guard<std::mutex> lock(mutex);
		pendingLoad = std::make_unique<PlayRequest>(request);
		pendingStop = false;
		state = PlayerState::Loading;
		eventId = request.eventId;
		eventName = request.eventName;
		error.clear();
	}
	cv.notify_all();
}

double ReplayPlayer::clockLocked(int64_t now) const
{
	if (state == PlayerState::Playing && !waitingForFrames)
		return clockBase + (double)(now - clockStart) / 1e6 * speed;
	return clockBase;
}

void ReplayPlayer::seekLocked(double position)
{
	if (decoders.empty())
		return;
	position = std::clamp(position, 0.0, std::max(0.0, duration));
	generation++;
	for (auto &decoder : decoders)
		if (decoder->isOk())
			decoder->seek(decoder->info().inOffset + position, generation);
	clockBase = position;
	clockStart = nowUsec();
	if (state == PlayerState::Ended)
		state = PlayerState::Paused;
	waitingForFrames = true;
	waitStarted = clockStart;
	angleDirty = true;
}

void ReplayPlayer::play()
{
	std::lock_guard<std::mutex> lock(mutex);
	if (pendingLoad) {
		pendingLoad->autoplay = true;
		return;
	}
	if (decoders.empty())
		return;
	if (state == PlayerState::Ended)
		seekLocked(0);
	if (state != PlayerState::Playing) {
		clockBase = clockLocked(nowUsec());
		clockStart = nowUsec();
		state = PlayerState::Playing;
		emitStarted = true;
	}
}

void ReplayPlayer::pause()
{
	std::lock_guard<std::mutex> lock(mutex);
	if (pendingLoad) {
		pendingLoad->autoplay = false;
		return;
	}
	if (state == PlayerState::Playing) {
		int64_t now = nowUsec();
		clockBase = clockLocked(now);
		clockStart = now;
		state = PlayerState::Paused;
		waitingForFrames = false;
	}
}

void ReplayPlayer::togglePause()
{
	bool playing;
	{
		std::lock_guard<std::mutex> lock(mutex);
		playing = pendingLoad ? pendingLoad->autoplay : state == PlayerState::Playing;
	}
	if (playing)
		pause();
	else
		play();
}

void ReplayPlayer::stop(bool clearFrame)
{
	{
		std::lock_guard<std::mutex> lock(mutex);
		pendingLoad.reset();
		pendingStop = true;
		pendingClear = pendingClear || clearFrame;
	}
	cv.notify_all();
}

void ReplayPlayer::restart()
{
	std::lock_guard<std::mutex> lock(mutex);
	if (decoders.empty())
		return;
	seekLocked(0);
	state = PlayerState::Playing;
	emitStarted = true;
}

void ReplayPlayer::setAngle(int angle)
{
	std::lock_guard<std::mutex> lock(mutex);
	if (pendingLoad) {
		pendingLoad->angle = angle;
		return;
	}
	if (angle >= 0 && angle < (int)decoders.size() && angle != selected) {
		selected = angle;
		angleDirty = true;
	}
}

void ReplayPlayer::setSpeed(double newSpeed)
{
	newSpeed = std::clamp(newSpeed, 0.05, 4.0);
	std::lock_guard<std::mutex> lock(mutex);
	if (pendingLoad)
		pendingLoad->speed = newSpeed;
	int64_t now = nowUsec();
	clockBase = clockLocked(now);
	clockStart = now;
	speed = newSpeed;
}

void ReplayPlayer::seek(double position)
{
	std::lock_guard<std::mutex> lock(mutex);
	if (pendingLoad) {
		pendingLoad->startAt = position;
		return;
	}
	seekLocked(position);
}

void ReplayPlayer::step(int frames)
{
	std::lock_guard<std::mutex> lock(mutex);
	if (decoders.empty() || frames == 0)
		return;
	int64_t now = nowUsec();
	double position = clockLocked(now) + frames * frameDuration;
	if (frames > 0 && state != PlayerState::Ended) {
		/* forward: the decoders are already ahead, just move the clock */
		clockBase = std::min(position, duration);
		clockStart = now;
		state = PlayerState::Paused;
		waitingForFrames = false;
	} else {
		state = PlayerState::Paused;
		seekLocked(position);
		waitingForFrames = false;
	}
}

void ReplayPlayer::setHoldLastFrame(bool hold)
{
	std::lock_guard<std::mutex> lock(mutex);
	holdLastFrame = hold;
}

PlayerStatus ReplayPlayer::status() const
{
	std::lock_guard<std::mutex> lock(mutex);
	PlayerStatus st;
	st.state = state;
	st.eventId = eventId;
	st.eventName = eventName;
	st.angle = selected;
	st.speed = speed;
	st.duration = duration;
	st.frameDuration = frameDuration;
	st.error = error;
	st.framesShown = framesShown;
	st.position = std::min(clockLocked(nowUsec()), duration);
	for (const auto &decoder : decoders)
		st.angleLabels.push_back(decoder->info().label);
	if (pendingLoad) {
		st.angleLabels.clear();
		for (const PlayAngle &a : pendingLoad->angles)
			st.angleLabels.push_back(a.label);
		st.angle = pendingLoad->angle;
		st.speed = pendingLoad->speed;
		st.position = 0;
	}
	return st;
}

void ReplayPlayer::run()
{
	for (;;) {
		std::unique_ptr<PlayRequest> request;
		bool stopRequested = false;
		{
			std::unique_lock<std::mutex> lock(mutex);
			cv.wait_for(lock, std::chrono::milliseconds(4),
				    [this]() { return quit || pendingLoad || pendingStop; });
			if (quit)
				break;
			request = std::move(pendingLoad);
			stopRequested = pendingStop;
			pendingStop = false;
		}

		if (stopRequested)
			doStop();
		if (request)
			doLoad(*request);
		tick();
	}
}

void ReplayPlayer::doStop()
{
	std::vector<std::unique_ptr<AngleDecoder>> old;
	bool clear;
	{
		std::lock_guard<std::mutex> lock(mutex);
		old.swap(decoders);
		generation++;
		state = PlayerState::Idle;
		eventId.clear();
		eventName.clear();
		duration = 0;
		clockBase = 0;
		framesShown = 0;
		clear = !holdLastFrame || pendingClear;
		pendingClear = false;
	}
	old.clear();
	for (AVFrame *&frame : current)
		av_frame_free(&frame);
	current.clear();
	if (clear)
		obs_source_output_video(obsSource, nullptr);
}

void ReplayPlayer::doLoad(const PlayRequest &request)
{
	std::vector<std::unique_ptr<AngleDecoder>> fresh;
	std::string firstError;
	double longest = 0;
	double shortestFrame = 0;
	bool anyOk = false;

	for (const PlayAngle &angle : request.angles) {
		auto decoder = std::make_unique<AngleDecoder>(angle, request.hwDecode, request.audio);
		std::string err;
		if (decoder->open(err)) {
			anyOk = true;
			longest = std::max(longest, decoder->duration() - angle.inOffset);
			double fd = decoder->frameDuration();
			if (fd > 0 && (shortestFrame == 0 || fd < shortestFrame))
				shortestFrame = fd;
		} else {
			obs_log(LOG_WARNING, "[player] %s", err.c_str());
			if (firstError.empty())
				firstError = err;
		}
		fresh.push_back(std::move(decoder));
	}

	std::vector<std::unique_ptr<AngleDecoder>> old;
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (pendingLoad || quit)
			return; /* superseded while files were opening */

		old.swap(decoders);
		decoders = std::move(fresh);
		generation++;
		duration = request.duration > 0 ? request.duration : longest;
		frameDuration = shortestFrame > 0 ? shortestFrame : 1.0 / 60.0;
		speed = std::clamp(request.speed, 0.05, 4.0);
		selected = std::clamp(request.angle, 0, std::max(0, (int)decoders.size() - 1));
		playAudio = request.audio;
		slowAudio = request.slowAudio;
		eventId = request.eventId;
		eventName = request.eventName;
		error = anyOk ? std::string() : firstError;
		state = !anyOk ? PlayerState::Error : request.autoplay ? PlayerState::Playing : PlayerState::Paused;
		clockBase = std::clamp(request.startAt, 0.0, std::max(0.0, duration));
		clockStart = nowUsec();
		waitingForFrames = true;
		waitStarted = clockStart;
		angleDirty = true;
		emitStarted = anyOk && request.autoplay;
		framesShown = 0;

		for (auto &decoder : decoders) {
			if (!decoder->isOk())
				continue;
			decoder->seek(decoder->info().inOffset + clockBase, generation);
			decoder->start();
		}
	}
	old.clear();

	for (AVFrame *&frame : current)
		av_frame_free(&frame);
	current.assign(request.angles.size(), nullptr);
}

static void outputVideo(obs_source_t *source, const AVFrame *frame)
{
	enum video_format format = convertPixelFormat(frame->format);
	if (format == VIDEO_FORMAT_NONE)
		return;

	struct obs_source_frame out = {};
	for (int i = 0; i < MAX_AV_PLANES && i < AV_NUM_DATA_POINTERS; i++) {
		out.data[i] = frame->data[i];
		out.linesize[i] = (uint32_t)std::abs(frame->linesize[i]);
	}
	out.width = (uint32_t)frame->width;
	out.height = (uint32_t)frame->height;
	out.format = format;
	out.timestamp = os_gettime_ns();
	out.trc = (uint8_t)convertTrc(frame->color_trc);

	enum video_colorspace cs = convertColorSpace(frame->colorspace, frame->color_trc, frame->color_primaries);
	enum video_range_type range = frame->color_range == AVCOL_RANGE_JPEG ? VIDEO_RANGE_FULL : VIDEO_RANGE_PARTIAL;
	if (frame->format == AV_PIX_FMT_YUVJ420P || frame->format == AV_PIX_FMT_YUVJ422P ||
	    frame->format == AV_PIX_FMT_YUVJ444P)
		range = VIDEO_RANGE_FULL;
	out.full_range = range == VIDEO_RANGE_FULL;
	video_format_get_parameters_for_format(cs, range, format, out.color_matrix, out.color_range_min,
					       out.color_range_max);
	obs_source_output_video(source, &out);
}

/* rateScale < 1 plays the samples slower, and lower, than they were recorded */
static void outputAudio(obs_source_t *source, const AVFrame *frame, uint64_t timestamp, double rateScale)
{
	enum audio_format format = convertSampleFormat(frame->format);
	const int channels = frame->ch_layout.nb_channels;
	enum speaker_layout speakers = convertSpeakers(channels);
	if (format == AUDIO_FORMAT_UNKNOWN || speakers == SPEAKERS_UNKNOWN)
		return;

	struct obs_source_audio out = {};
	const bool planar = av_sample_fmt_is_planar((enum AVSampleFormat)frame->format);
	const int planes = planar ? std::min(channels, MAX_AV_PLANES) : 1;
	for (int i = 0; i < planes; i++)
		out.data[i] = frame->extended_data[i];
	out.frames = (uint32_t)frame->nb_samples;
	out.speakers = speakers;
	out.format = format;
	out.samples_per_sec = (uint32_t)std::max(1L, std::lround(frame->sample_rate * rateScale));
	out.timestamp = timestamp;
	obs_source_output_audio(source, &out);
}

/* Converts any decoded sample format to interleaved float. */
static bool toInterleavedFloat(const AVFrame *frame, std::vector<float> &out)
{
	const int channels = frame->ch_layout.nb_channels;
	const int samples = frame->nb_samples;
	const enum AVSampleFormat format = (enum AVSampleFormat)frame->format;
	const enum AVSampleFormat packed = av_get_packed_sample_fmt(format);
	const bool planar = av_sample_fmt_is_planar(format);
	if (channels <= 0 || samples <= 0 || !frame->extended_data)
		return false;

	out.resize((size_t)channels * (size_t)samples);
	for (int c = 0; c < channels; c++) {
		const uint8_t *base = frame->extended_data[planar ? c : 0];
		for (int i = 0; i < samples; i++) {
			const size_t at = planar ? (size_t)i : (size_t)i * (size_t)channels + (size_t)c;
			float value;
			switch (packed) {
			case AV_SAMPLE_FMT_U8:
				value = ((float)base[at] - 128.0f) / 128.0f;
				break;
			case AV_SAMPLE_FMT_S16:
				value = (float)reinterpret_cast<const int16_t *>(base)[at] / 32768.0f;
				break;
			case AV_SAMPLE_FMT_S32:
				value = (float)reinterpret_cast<const int32_t *>(base)[at] / 2147483648.0f;
				break;
			case AV_SAMPLE_FMT_FLT:
				value = reinterpret_cast<const float *>(base)[at];
				break;
			case AV_SAMPLE_FMT_DBL:
				value = (float)reinterpret_cast<const double *>(base)[at];
				break;
			default:
				return false;
			}
			out[(size_t)i * (size_t)channels + (size_t)c] = value;
		}
	}
	return true;
}

void ReplayPlayer::outputStretched(const AVFrame *frame, double ahead, double speedNow)
{
	const int channels = frame->ch_layout.nb_channels;
	const enum speaker_layout speakers = convertSpeakers(channels);
	if (speakers == SPEAKERS_UNKNOWN || frame->sample_rate <= 0 || !toInterleavedFloat(frame, stretchIn))
		return;

	if (!stretching || stretcher.sampleRate() != frame->sample_rate || stretcher.channels() != channels) {
		stretcher.configure(frame->sample_rate, channels, speedNow);
		stretchStartNs = os_gettime_ns() + (uint64_t)(std::max(0.0, ahead) * 1e9);
		stretchFrames = 0;
		stretching = true;
	}

	stretchOut.clear();
	stretcher.push(stretchIn.data(), (size_t)frame->nb_samples, stretchOut);
	const size_t frames = stretchOut.size() / (size_t)channels;
	if (!frames)
		return;

	struct obs_source_audio out = {};
	out.data[0] = reinterpret_cast<const uint8_t *>(stretchOut.data());
	out.frames = (uint32_t)frames;
	out.speakers = speakers;
	out.format = AUDIO_FORMAT_FLOAT;
	out.samples_per_sec = (uint32_t)frame->sample_rate;
	/* stretched audio is continuous: each block follows the last one */
	out.timestamp = stretchStartNs + (uint64_t)((double)stretchFrames * 1e9 / frame->sample_rate);
	obs_source_output_audio(obsSource, &out);
	stretchFrames += frames;
}

void ReplayPlayer::tick()
{
	const AVFrame *show = nullptr;
	std::vector<QueuedFrame> audioOut;
	bool started = false;
	bool ended = false;
	bool audible = false;
	double position = 0;
	double selectedOffset = 0;
	double speedNow = 1.0;
	uint64_t gen = 0;
	int angleNow = 0;
	SlowAudio slowMode = SlowAudio::Off;
	const int64_t now = nowUsec();

	{
		std::lock_guard<std::mutex> lock(mutex);
		if (decoders.empty())
			return;
		if (current.size() != decoders.size())
			current.resize(decoders.size(), nullptr);

		AngleDecoder *sel = decoders[(size_t)selected].get();
		if (waitingForFrames) {
			bool ready = !sel->isOk() || sel->hasVideo(generation);
			if (ready || now - waitStarted > kPrerollTimeoutUsec) {
				waitingForFrames = false;
				clockStart = now;
			}
		}

		position = clockLocked(now);
		if (state == PlayerState::Playing && !waitingForFrames && position >= duration) {
			position = duration;
			clockBase = duration;
			clockStart = now;
			state = PlayerState::Ended;
			ended = true;
		}

		const bool normalSpeed = std::fabs(speed - 1.0) < 0.001;
		audible = playAudio && state == PlayerState::Playing && !waitingForFrames &&
			  (normalSpeed || slowAudio != SlowAudio::Off);
		/* the stretcher needs ~50 ms of input beyond what it plays */
		const double lookahead = normalSpeed ? 0.06 : 0.12;
		for (size_t i = 0; i < decoders.size(); i++) {
			AngleDecoder *decoder = decoders[i].get();
			if (!decoder->isOk())
				continue;
			const double target = decoder->info().inOffset + position;
			if (AVFrame *frame = decoder->popVideoUntil(target + frameDuration * 0.5, generation)) {
				av_frame_free(&current[i]);
				current[i] = frame;
				if ((int)i == selected)
					angleDirty = true;
			}
			const bool wanted = audible && (int)i == selected;
			decoder->popAudioUntil(target + (wanted ? lookahead : 0.0), generation,
					       wanted ? &audioOut : nullptr);
		}
		speedNow = speed;
		gen = generation;
		angleNow = selected;
		slowMode = slowAudio;

		if (angleDirty && current[(size_t)selected]) {
			show = current[(size_t)selected];
			angleDirty = false;
			framesShown++;
		}
		selectedOffset = sel->info().inOffset;
		started = emitStarted;
		emitStarted = false;
	}

	/* current[] is only modified on this thread, so `show` stays valid */
	if (show)
		outputVideo(obsSource, show);

	const bool normalSpeed = std::fabs(speedNow - 1.0) < 0.001;
	const bool stretch = audible && !normalSpeed && slowMode == SlowAudio::Natural;
	if (!stretch || gen != stretchGeneration || angleNow != stretchAngle || speedNow != stretchSpeed) {
		/* the next stretched frame starts afresh at the new position */
		stretching = false;
		stretchGeneration = gen;
		stretchAngle = angleNow;
		stretchSpeed = speedNow;
	}

	const uint64_t nowNs = os_gettime_ns();
	for (QueuedFrame &q : audioOut) {
		/* file seconds until this frame plays, in real seconds at this speed */
		const double ahead = (q.time - selectedOffset - position) / speedNow;
		if (stretch) {
			outputStretched(q.frame, ahead, speedNow);
		} else {
			uint64_t ts = ahead > 0 ? nowNs + (uint64_t)(ahead * 1e9) : nowNs;
			outputAudio(obsSource, q.frame, ts, normalSpeed ? 1.0 : speedNow);
		}
		av_frame_free(&q.frame);
	}

	if (started)
		obs_source_media_started(obsSource);
	if (ended)
		obs_source_media_ended(obsSource);
}

/* ------------------------------------------------------------------------- */
/* OBS source glue                                                           */

namespace {

void getPlayerProc(void *data, calldata_t *cd)
{
	calldata_set_ptr(cd, "player", data);
}

const char *playerName(void *)
{
	return text("Player.Name");
}

void *playerCreate(obs_data_t *settings, obs_source_t *source)
{
	auto *player = new ReplayPlayer(source);
	player->setHoldLastFrame(obs_data_get_bool(settings, "hold_last_frame"));
	obs_source_set_async_unbuffered(source, true);
	proc_handler_add(obs_source_get_proc_handler(source), "void get_player(out ptr player)", getPlayerProc, player);
	return player;
}

void playerDestroy(void *data)
{
	delete static_cast<ReplayPlayer *>(data);
}

void playerDefaults(obs_data_t *settings)
{
	obs_data_set_default_bool(settings, "hold_last_frame", true);
}

obs_properties_t *playerProperties(void *)
{
	obs_properties_t *props = obs_properties_create();
	obs_properties_add_bool(props, "hold_last_frame", text("Player.HoldLastFrame"));
	obs_property_t *info = obs_properties_add_text(props, "info", text("Player.Info"), OBS_TEXT_INFO);
	obs_property_text_set_info_type(info, OBS_TEXT_INFO_NORMAL);
	return props;
}

void playerUpdate(void *data, obs_data_t *settings)
{
	static_cast<ReplayPlayer *>(data)->setHoldLastFrame(obs_data_get_bool(settings, "hold_last_frame"));
}

void mediaPlayPause(void *data, bool pause)
{
	auto *player = static_cast<ReplayPlayer *>(data);
	if (pause)
		player->pause();
	else
		player->play();
}

void mediaRestart(void *data)
{
	static_cast<ReplayPlayer *>(data)->restart();
}

void mediaStop(void *data)
{
	static_cast<ReplayPlayer *>(data)->stop();
}

void mediaNext(void *data)
{
	auto *player = static_cast<ReplayPlayer *>(data);
	PlayerStatus st = player->status();
	if (!st.angleLabels.empty())
		player->setAngle((st.angle + 1) % (int)st.angleLabels.size());
}

void mediaPrevious(void *data)
{
	auto *player = static_cast<ReplayPlayer *>(data);
	PlayerStatus st = player->status();
	int count = (int)st.angleLabels.size();
	if (count)
		player->setAngle((st.angle + count - 1) % count);
}

int64_t mediaDuration(void *data)
{
	return (int64_t)(static_cast<ReplayPlayer *>(data)->status().duration * 1000.0);
}

int64_t mediaTime(void *data)
{
	return (int64_t)(static_cast<ReplayPlayer *>(data)->status().position * 1000.0);
}

void mediaSetTime(void *data, int64_t ms)
{
	static_cast<ReplayPlayer *>(data)->seek((double)ms / 1000.0);
}

enum obs_media_state mediaState(void *data)
{
	switch (static_cast<ReplayPlayer *>(data)->status().state) {
	case PlayerState::Idle:
		return OBS_MEDIA_STATE_STOPPED;
	case PlayerState::Loading:
		return OBS_MEDIA_STATE_OPENING;
	case PlayerState::Playing:
		return OBS_MEDIA_STATE_PLAYING;
	case PlayerState::Paused:
		return OBS_MEDIA_STATE_PAUSED;
	case PlayerState::Ended:
		return OBS_MEDIA_STATE_ENDED;
	case PlayerState::Error:
		return OBS_MEDIA_STATE_ERROR;
	}
	return OBS_MEDIA_STATE_NONE;
}

} // namespace

void registerPlayerSource()
{
	obs_source_info info = {};
	info.id = kPlayerSourceId;
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_AUDIO | OBS_SOURCE_DO_NOT_DUPLICATE |
			    OBS_SOURCE_CONTROLLABLE_MEDIA;
	info.get_name = playerName;
	info.create = playerCreate;
	info.destroy = playerDestroy;
	info.get_defaults = playerDefaults;
	info.get_properties = playerProperties;
	info.update = playerUpdate;
	info.icon_type = OBS_ICON_TYPE_MEDIA;
	info.media_play_pause = mediaPlayPause;
	info.media_restart = mediaRestart;
	info.media_stop = mediaStop;
	info.media_next = mediaNext;
	info.media_previous = mediaPrevious;
	info.media_get_duration = mediaDuration;
	info.media_get_time = mediaTime;
	info.media_set_time = mediaSetTime;
	info.media_get_state = mediaState;
	obs_register_source(&info);
}

ReplayPlayer *playerFromSource(obs_source_t *source)
{
	if (!source || strcmp(obs_source_get_unversioned_id(source), kPlayerSourceId) != 0)
		return nullptr;
	calldata_t cd;
	calldata_init(&cd);
	ReplayPlayer *player = nullptr;
	if (proc_handler_call(obs_source_get_proc_handler(source), "get_player", &cd))
		player = static_cast<ReplayPlayer *>(calldata_ptr(&cd, "player"));
	calldata_free(&cd);
	return player;
}

} // namespace rc
