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

#include "clip-writer.hpp"
#include "ffmpeg-compat.hpp"
#include "rc-common.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <limits>
#include <system_error>

namespace fs = std::filesystem;

namespace rc {

namespace {

const AVRational kMicros = {1, 1000000};

bool setExtradata(AVCodecParameters *par, const std::vector<uint8_t> &data)
{
	if (data.empty())
		return true;
	par->extradata = (uint8_t *)av_mallocz(data.size() + AV_INPUT_BUFFER_PADDING_SIZE);
	if (!par->extradata)
		return false;
	memcpy(par->extradata, data.data(), data.size());
	par->extradata_size = (int)data.size();
	return true;
}

struct OutputFile {
	AVFormatContext *ctx = nullptr;
	bool headerWritten = false;

	~OutputFile()
	{
		if (!ctx)
			return;
		if (headerWritten)
			av_write_trailer(ctx);
		if (ctx->pb)
			avio_closep(&ctx->pb);
		avformat_free_context(ctx);
	}

	bool finish(std::string &error)
	{
		int ret = av_write_trailer(ctx);
		headerWritten = false;
		if (ret < 0) {
			error = "could not finish file: " + averr(ret);
			return false;
		}
		return true;
	}
};

void removeQuietly(const std::string &path)
{
	std::error_code ec;
	fs::remove(fs::u8path(path), ec);
}

/* Tags the file with the canvas color space so players (and the Replay Cue
 * Player) show SDR and HDR replays correctly. */
void setColorTags(AVCodecParameters *par, const StreamInfo &info)
{
	switch (info.colorspace) {
	case VIDEO_CS_601:
		par->color_primaries = AVCOL_PRI_SMPTE170M;
		par->color_trc = AVCOL_TRC_SMPTE170M;
		par->color_space = AVCOL_SPC_SMPTE170M;
		break;
	case VIDEO_CS_SRGB:
		par->color_primaries = AVCOL_PRI_BT709;
		par->color_trc = AVCOL_TRC_IEC61966_2_1;
		par->color_space = AVCOL_SPC_BT709;
		break;
	case VIDEO_CS_2100_PQ:
		par->color_primaries = AVCOL_PRI_BT2020;
		par->color_trc = AVCOL_TRC_SMPTE2084;
		par->color_space = AVCOL_SPC_BT2020_NCL;
		break;
	case VIDEO_CS_2100_HLG:
		par->color_primaries = AVCOL_PRI_BT2020;
		par->color_trc = AVCOL_TRC_ARIB_STD_B67;
		par->color_space = AVCOL_SPC_BT2020_NCL;
		break;
	case VIDEO_CS_DEFAULT:
	case VIDEO_CS_709:
	default:
		par->color_primaries = AVCOL_PRI_BT709;
		par->color_trc = AVCOL_TRC_BT709;
		par->color_space = AVCOL_SPC_BT709;
		break;
	}
	par->color_range = info.fullRange ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
}

struct InputFile {
	AVFormatContext *ctx = nullptr;
	~InputFile() { avformat_close_input(&ctx); }
};

/* Exports run one at a time on the writer thread, so picking the name here
 * (not when the export was queued) cannot clash with another export. */
std::string freePath(const std::string &wanted)
{
	std::error_code ec;
	if (!fs::exists(fs::u8path(wanted), ec))
		return wanted;
	const size_t dot = wanted.find_last_of('.');
	const size_t slash = wanted.find_last_of("/\\");
	const bool hasExt = dot != std::string::npos && (slash == std::string::npos || dot > slash);
	const std::string base = hasExt ? wanted.substr(0, dot) : wanted;
	const std::string ext = hasExt ? wanted.substr(dot) : std::string();
	for (int n = 2;; n++) {
		std::string candidate = base + "_" + std::to_string(n) + ext;
		if (!fs::exists(fs::u8path(candidate), ec))
			return candidate;
	}
}

/* Deletes a half-written file; declared before the OutputFile so the file is
 * closed first. */
struct RemoveUnlessKept {
	std::string path;
	bool keep = false;
	~RemoveUnlessKept()
	{
		if (!keep && !path.empty())
			removeQuietly(path);
	}
};

bool sameExtradata(const AVCodecParameters *a, const AVCodecParameters *b)
{
	return a->extradata_size == b->extradata_size &&
	       (a->extradata_size == 0 || memcmp(a->extradata, b->extradata, (size_t)a->extradata_size) == 0);
}

/* Packets can only be joined into one stream when the decoder setup is the
 * same: same codec, size and parameter sets (SPS/PPS). */
bool sameVideo(const AVCodecParameters *a, const AVCodecParameters *b)
{
	return a->codec_id == b->codec_id && a->width == b->width && a->height == b->height && sameExtradata(a, b);
}

bool sameAudio(const AVCodecParameters *a, const AVCodecParameters *b)
{
	return a->codec_id == b->codec_id && a->sample_rate == b->sample_rate &&
	       a->ch_layout.nb_channels == b->ch_layout.nb_channels && sameExtradata(a, b);
}

AVStream *addStreamLike(AVFormatContext *out, const AVStream *ist)
{
	AVStream *ost = avformat_new_stream(out, nullptr);
	if (!ost || avcodec_parameters_copy(ost->codecpar, ist->codecpar) < 0)
		return nullptr;
	ost->codecpar->codec_tag = 0;
	if (ost->codecpar->codec_id == AV_CODEC_ID_HEVC)
		ost->codecpar->codec_tag = MKTAG('h', 'v', 'c', '1');
	ost->time_base = ist->time_base;
	ost->avg_frame_rate = ist->avg_frame_rate;
	return ost;
}

} // namespace

bool writeClipFile(const std::string &path, const PacketList &video, const PacketList *audio, int64_t &firstFrameUsec,
		   double &duration, std::string &error)
{
	if (!ffmpegReady()) {
		error = "FFmpeg libraries are not compatible with this OBS version";
		return false;
	}
	if (video.packets.empty()) {
		error = "no video in the buffer for that moment";
		return false;
	}

	const StreamInfo &vi = video.info;
	const AVCodecID videoCodec = codecIdFromObs(vi.codec.c_str());
	if (videoCodec == AV_CODEC_ID_NONE) {
		error = "unsupported video codec '" + vi.codec + "'";
		return false;
	}
	if (vi.extradata.empty() && videoCodec != AV_CODEC_ID_AV1) {
		error = "the video encoder has not produced its headers yet";
		return false;
	}

	const encoder_packet &p0 = video.packets.front().pkt;
	const AVRational vtb = {p0.timebase_num, p0.timebase_den};
	const int64_t dts0 = p0.dts;
	/* file timestamp 0 is encoder tick dts0; find its capture time */
	const int64_t zeroUsec = video.packets.front().ptsUsec - av_rescale_q(p0.pts - dts0, vtb, kMicros);

	OutputFile out;
	int ret = avformat_alloc_output_context2(&out.ctx, nullptr, "mp4", path.c_str());
	if (ret < 0 || !out.ctx) {
		error = "could not create MP4 muxer: " + averr(ret);
		return false;
	}

	AVStream *vs = avformat_new_stream(out.ctx, nullptr);
	if (!vs) {
		error = "could not add video stream";
		return false;
	}
	vs->time_base = vtb;
	if (vi.fpsNum && vi.fpsDen)
		vs->avg_frame_rate = AVRational{(int)vi.fpsNum, (int)vi.fpsDen};
	AVCodecParameters *vp = vs->codecpar;
	vp->codec_type = AVMEDIA_TYPE_VIDEO;
	vp->codec_id = videoCodec;
	vp->width = (int)vi.width;
	vp->height = (int)vi.height;
	if (videoCodec == AV_CODEC_ID_HEVC)
		vp->codec_tag = MKTAG('h', 'v', 'c', '1'); /* plays in QuickTime and on phones */
	setColorTags(vp, vi);
	if (!setExtradata(vp, vi.extradata)) {
		error = "out of memory";
		return false;
	}

	AVStream *as = nullptr;
	int audioRate = 0;
	int audioFrameSize = 1024;
	if (audio && !audio->packets.empty()) {
		const StreamInfo &ai = audio->info;
		AVCodecID audioCodec = codecIdFromObs(ai.codec.c_str());
		if (audioCodec != AV_CODEC_ID_NONE && ai.sampleRate && ai.channels) {
			as = avformat_new_stream(out.ctx, nullptr);
			if (as) {
				audioRate = (int)ai.sampleRate;
				audioFrameSize = audioCodec == AV_CODEC_ID_OPUS ? 960 : 1024;
				as->time_base = AVRational{1, audioRate};
				AVCodecParameters *ap = as->codecpar;
				ap->codec_type = AVMEDIA_TYPE_AUDIO;
				ap->codec_id = audioCodec;
				ap->sample_rate = audioRate;
				ap->frame_size = audioFrameSize;
				av_channel_layout_default(&ap->ch_layout, (int)ai.channels);
				setExtradata(ap, ai.extradata);
			}
		}
	}

	ret = avio_open(&out.ctx->pb, path.c_str(), AVIO_FLAG_WRITE);
	if (ret < 0) {
		error = "could not open " + path + ": " + averr(ret);
		return false;
	}
	ret = avformat_write_header(out.ctx, nullptr);
	if (ret < 0) {
		error = "could not write MP4 header: " + averr(ret);
		return false;
	}
	out.headerWritten = true;

	struct Item {
		int64_t key;
		bool video;
		size_t index;
	};
	std::vector<Item> order;
	order.reserve(video.packets.size() + (as ? audio->packets.size() : 0));
	for (size_t i = 0; i < video.packets.size(); i++)
		order.push_back({video.packets[i].dtsUsec, true, i});
	if (as)
		for (size_t i = 0; i < audio->packets.size(); i++)
			order.push_back({audio->packets[i].ptsUsec, false, i});
	std::stable_sort(order.begin(), order.end(), [](const Item &a, const Item &b) { return a.key < b.key; });

	int64_t lastAudioTs = std::numeric_limits<int64_t>::min();
	for (const Item &item : order) {
		AVPacket *pkt = av_packet_alloc();
		if (item.video) {
			const encoder_packet &e = video.packets[item.index].pkt;
			pkt->data = e.data;
			pkt->size = (int)e.size;
			pkt->stream_index = vs->index;
			pkt->pts = av_rescale_q(e.pts - dts0, vtb, vs->time_base);
			pkt->dts = av_rescale_q(e.dts - dts0, vtb, vs->time_base);
			pkt->duration = av_rescale_q(1, vtb, vs->time_base);
			if (e.keyframe)
				pkt->flags |= AV_PKT_FLAG_KEY;
		} else {
			const TimedPacket &t = audio->packets[item.index];
			int64_t ts = av_rescale(t.ptsUsec - zeroUsec, audioRate, 1000000);
			if (ts < 0 || ts <= lastAudioTs) {
				av_packet_free(&pkt);
				continue;
			}
			lastAudioTs = ts;
			const AVRational atb = {1, audioRate};
			pkt->data = t.pkt.data;
			pkt->size = (int)t.pkt.size;
			pkt->stream_index = as->index;
			pkt->pts = pkt->dts = av_rescale_q(ts, atb, as->time_base);
			pkt->duration = av_rescale_q(audioFrameSize, atb, as->time_base);
			pkt->flags |= AV_PKT_FLAG_KEY;
		}

		ret = av_interleaved_write_frame(out.ctx, pkt);
		av_packet_free(&pkt);
		if (ret < 0) {
			error = "could not write packet: " + averr(ret);
			return false;
		}
	}

	if (!out.finish(error))
		return false;

	int64_t first = std::numeric_limits<int64_t>::max();
	int64_t last = std::numeric_limits<int64_t>::min();
	for (const TimedPacket &p : video.packets) {
		first = std::min(first, p.ptsUsec);
		last = std::max(last, p.ptsUsec);
	}
	firstFrameUsec = first;
	duration = (double)(last - first) / 1e6 + av_q2d(vtb);
	return true;
}

bool exportReel(const std::vector<ExportItem> &segments, std::string &output, std::vector<size_t> &skipped,
		double &duration, std::string &error)
{
	skipped.clear();
	duration = 0;
	if (!ffmpegReady()) {
		error = "FFmpeg libraries are not compatible with this OBS version";
		return false;
	}
	if (segments.empty()) {
		error = "nothing to export";
		return false;
	}

	std::error_code ec;
	fs::create_directories(fs::u8path(dirNameOf(output)), ec);

	/* the final name must be known up front: +faststart re-reads the file
	 * by the muxer's own name when it moves the index to the front */
	output = freePath(output);
	RemoveUnlessKept cleanup;
	OutputFile out;
	int ret = avformat_alloc_output_context2(&out.ctx, nullptr, "mp4", output.c_str());
	if (ret < 0 || !out.ctx) {
		error = "could not create MP4 muxer: " + averr(ret);
		return false;
	}

	AVStream *vout = nullptr;
	AVStream *aout = nullptr;
	int64_t baseUs = 0; /* where the next segment starts in the output */
	int64_t lastVideoDts = std::numeric_limits<int64_t>::min();
	int64_t lastAudioDts = std::numeric_limits<int64_t>::min();
	size_t written = 0;
	std::string firstError;

	for (size_t s = 0; s < segments.size(); s++) {
		const ExportItem &item = segments[s];
		InputFile in;
		ret = avformat_open_input(&in.ctx, item.input.c_str(), nullptr, nullptr);
		if (ret >= 0)
			ret = avformat_find_stream_info(in.ctx, nullptr);
		const int vidx = ret >= 0 ? av_find_best_stream(in.ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0) : -1;
		if (vidx < 0) {
			if (firstError.empty())
				firstError = ret < 0 ? "could not read " + item.input + ": " + averr(ret)
						     : item.input + " has no video";
			skipped.push_back(s);
			continue;
		}
		const int aidx = av_find_best_stream(in.ctx, AVMEDIA_TYPE_AUDIO, -1, vidx, nullptr, 0);
		AVStream *vin = in.ctx->streams[vidx];
		AVStream *ain = aidx >= 0 ? in.ctx->streams[aidx] : nullptr;

		if (!vout) {
			/* the first readable segment sets up the file */
			vout = addStreamLike(out.ctx, vin);
			aout = ain ? addStreamLike(out.ctx, ain) : nullptr;
			if (!vout || (ain && !aout)) {
				error = "could not copy stream parameters";
				return false;
			}
			ret = avio_open(&out.ctx->pb, output.c_str(), AVIO_FLAG_WRITE);
			if (ret < 0) {
				error = "could not create " + output + ": " + averr(ret);
				return false;
			}
			cleanup.path = output;
			AVDictionary *opts = nullptr;
			av_dict_set(&opts, "movflags", "+faststart", 0);
			ret = avformat_write_header(out.ctx, &opts);
			av_dict_free(&opts);
			if (ret < 0) {
				error = "could not write MP4 header: " + averr(ret);
				return false;
			}
			out.headerWritten = true;
		} else if (!sameVideo(vin->codecpar, vout->codecpar) || (ain != nullptr) != (aout != nullptr) ||
			   (ain && !sameAudio(ain->codecpar, aout->codecpar))) {
			if (firstError.empty())
				firstError = item.input + " does not match the other clips";
			skipped.push_back(s);
			continue;
		}

		const int64_t originUs =
			vin->start_time != AV_NOPTS_VALUE ? av_rescale_q(vin->start_time, vin->time_base, kMicros) : 0;
		const int64_t startUs = originUs + (int64_t)(item.start * 1e6);
		const bool bounded = item.end > item.start;
		const int64_t endUs = originUs + (int64_t)(item.end * 1e6);
		if (item.start > 0)
			av_seek_frame(in.ctx, vidx, av_rescale_q(startUs, kMicros, vin->time_base),
				      AVSEEK_FLAG_BACKWARD);

		AVPacket *pkt = av_packet_alloc();
		int64_t offsetUs = AV_NOPTS_VALUE;
		int64_t segmentUs = 0; /* length of this segment's video */
		bool videoDone = false;
		bool audioDone = !ain;
		while (!(videoDone && audioDone) && av_read_frame(in.ctx, pkt) >= 0) {
			const bool isVideo = pkt->stream_index == vidx;
			const bool isAudio = ain && pkt->stream_index == aidx;
			if ((!isVideo && !isAudio) || (pkt->pts == AV_NOPTS_VALUE && pkt->dts == AV_NOPTS_VALUE)) {
				av_packet_unref(pkt);
				continue;
			}

			AVStream *ist = in.ctx->streams[pkt->stream_index];
			AVStream *ost = isVideo ? vout : aout;
			const int64_t ptsUs =
				av_rescale_q(pkt->pts != AV_NOPTS_VALUE ? pkt->pts : pkt->dts, ist->time_base, kMicros);
			const int64_t dtsUs =
				av_rescale_q(pkt->dts != AV_NOPTS_VALUE ? pkt->dts : pkt->pts, ist->time_base, kMicros);

			if (offsetUs == AV_NOPTS_VALUE) {
				/* start at the first video packet: a backward seek lands on a
				 * keyframe, but the MP4 demuxer does not always flag it as one */
				if (!isVideo) {
					av_packet_unref(pkt);
					continue;
				}
				offsetUs = dtsUs;
			}

			if (bounded && ptsUs > endUs) {
				(isVideo ? videoDone : audioDone) = true;
				av_packet_unref(pkt);
				continue;
			}
			if (ptsUs < offsetUs) {
				av_packet_unref(pkt);
				continue;
			}

			if (isVideo)
				segmentUs = std::max(
					segmentUs, ptsUs - offsetUs +
							   std::max<int64_t>(1, av_rescale_q(pkt->duration,
											     ist->time_base, kMicros)));
			pkt->pts = av_rescale_q(ptsUs - offsetUs + baseUs, kMicros, ost->time_base);
			pkt->dts = av_rescale_q(dtsUs - offsetUs + baseUs, kMicros, ost->time_base);
			int64_t &lastDts = isVideo ? lastVideoDts : lastAudioDts;
			if (pkt->dts <= lastDts) {
				if (!isVideo) {
					/* audio running past the previous clip's last frame */
					av_packet_unref(pkt);
					continue;
				}
				const int64_t shift = lastDts + 1 - pkt->dts;
				pkt->dts += shift;
				pkt->pts += shift;
			}
			lastDts = pkt->dts;
			pkt->duration = av_rescale_q(pkt->duration, ist->time_base, ost->time_base);
			pkt->pos = -1;
			pkt->stream_index = ost->index;
			ret = av_interleaved_write_frame(out.ctx, pkt);
			if (ret < 0) {
				av_packet_free(&pkt);
				error = "could not write " + output + ": " + averr(ret);
				return false;
			}
		}
		av_packet_free(&pkt);

		if (offsetUs == AV_NOPTS_VALUE) {
			if (firstError.empty())
				firstError = item.input + " has no video in that range";
			skipped.push_back(s);
			continue;
		}
		baseUs += segmentUs;
		written++;
	}

	if (!written) {
		error = firstError.empty() ? "nothing could be exported" : firstError;
		return false;
	}
	duration = (double)baseUs / 1e6;
	cleanup.keep = out.finish(error);
	return cleanup.keep;
}

bool exportClip(const ExportItem &item, std::string &error)
{
	std::vector<size_t> skipped;
	double duration = 0;
	std::string output = item.output;
	return exportReel({item}, output, skipped, duration, error);
}

/* ------------------------------------------------------------------------- */

ClipWriter::ClipWriter(ClipCallback callback) : onClip(std::move(callback))
{
	worker = std::thread([this]() { run(); });
}

ClipWriter::~ClipWriter()
{
	quitting = true;
	cv.notify_all();
	if (worker.joinable())
		worker.join();
}

void ClipWriter::run()
{
	for (;;) {
		std::function<void()> task;
		{
			std::unique_lock<std::mutex> lock(mutex);
			cv.wait(lock, [this]() { return quitting || !tasks.empty(); });
			if (tasks.empty())
				return;
			task = std::move(tasks.front());
			tasks.pop_front();
		}
		task();
	}
}

void ClipWriter::enqueue(ClipJob job)
{
	{
		std::lock_guard<std::mutex> lock(mutex);
		tasks.push_back([this, job]() mutable {
			ClipResult result = process(job);
			if (onClip)
				onClip(std::move(result));
		});
	}
	cv.notify_one();
}

void ClipWriter::enqueueExport(std::vector<ExportItem> items, ExportCallback done)
{
	{
		std::lock_guard<std::mutex> lock(mutex);
		tasks.push_back([items, done]() {
			std::string errors;
			size_t written = 0;
			for (const ExportItem &item : items) {
				std::string err;
				if (exportClip(item, err))
					written++;
				else
					errors += err + "\n";
			}
			if (done) {
				if (errors.empty())
					done(true,
					     std::to_string(written) + " file(s) exported to " +
						     dirNameOf(items.empty() ? std::string() : items.front().output));
				else
					done(false, errors);
			}
		});
	}
	cv.notify_one();
}

void ClipWriter::enqueueReel(std::vector<ExportItem> segments, std::string output, ReelCallback done)
{
	{
		std::lock_guard<std::mutex> lock(mutex);
		tasks.push_back([segments, output, done]() {
			ReelResult result;
			result.output = output;
			result.ok = exportReel(segments, result.output, result.skipped, result.duration, result.error);
			if (done)
				done(std::move(result));
		});
	}
	cv.notify_one();
}

ClipResult ClipWriter::process(ClipJob &job)
{
	ClipResult result;
	result.playWhenReady = job.playWhenReady;

	/* encoders run a few frames behind real time: wait until every angle has
	 * delivered the OUT point before cutting */
	const int64_t want = job.outUsec + job.tailUsec;
	const int64_t deadline = nowUsec() + 3000000;
	while (!quitting) {
		bool ready = true;
		for (const auto &angle : job.angles)
			if (angle.buffer->newestUsec() < want)
				ready = false;
		if (ready || nowUsec() > deadline)
			break;
		std::this_thread::sleep_for(std::chrono::milliseconds(15));
	}

	std::error_code ec;
	fs::create_directories(fs::u8path(job.folder), ec);

	ReplayEvent event;
	event.id = "rc:" + job.baseName;
	event.name = job.name;
	event.tag = job.tag;
	event.highlight = job.highlight;
	event.period = job.period;
	event.match = job.match;
	event.folder = job.folder;
	event.number = job.number;
	event.created = job.created;
	event.outUsec = job.outUsec;
	event.sidecar = joinPath(job.folder, job.baseName + kSidecarExtension);

	int64_t latestStart = job.inUsec;
	int64_t earliestEnd = job.outUsec;
	for (const auto &angle : job.angles) {
		PacketList videoPackets;
		if (!angle.buffer->snapshot(job.inUsec, want, videoPackets))
			continue;

		PacketList audioPackets;
		const PacketList *audioPtr = nullptr;
		if (job.audio && job.audio->snapshot(videoPackets.packets.front().ptsUsec, want, audioPackets))
			audioPtr = &audioPackets;

		const std::string file =
			joinPath(job.folder, job.baseName + "_" + sanitizeFileName(angle.label) + ".mp4");
		const std::string part = file + ".part";

		AngleClip clip;
		clip.label = angle.label;
		clip.file = file;
		std::string error;
		if (!writeClipFile(part, videoPackets, audioPtr, clip.firstFrameUsec, clip.duration, error)) {
			obs_log(LOG_WARNING, "[%s] %s", angle.label.c_str(), error.c_str());
			removeQuietly(part);
			result.error = error;
			continue;
		}
		fs::rename(fs::u8path(part), fs::u8path(file), ec);
		if (ec) {
			removeQuietly(part);
			result.error = "could not rename " + part + ": " + ec.message();
			continue;
		}

		latestStart = std::max(latestStart, clip.firstFrameUsec);
		earliestEnd = std::min(earliestEnd, clip.firstFrameUsec + (int64_t)(clip.duration * 1e6));
		event.angles.push_back(clip);
	}

	if (event.angles.empty()) {
		if (result.error.empty())
			result.error = "none of the cameras had video for that moment";
		return result;
	}

	/* every angle must have media at IN, so start where the latest one does */
	event.inUsec = latestStart;
	event.duration = std::max(0.1, (double)(earliestEnd - latestStart) / 1e6);

	std::string error;
	if (!saveSidecar(event, error)) {
		result.error = error;
		return result;
	}

	result.ok = true;
	result.error.clear();
	result.event = std::move(event);
	return result;
}

} // namespace rc
