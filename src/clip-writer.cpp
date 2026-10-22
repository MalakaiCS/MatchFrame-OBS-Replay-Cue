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

bool exportClip(const ExportItem &item, std::string &error)
{
	if (!ffmpegReady()) {
		error = "FFmpeg libraries are not compatible with this OBS version";
		return false;
	}

	AVFormatContext *in = nullptr;
	int ret = avformat_open_input(&in, item.input.c_str(), nullptr, nullptr);
	if (ret < 0) {
		error = "could not open " + item.input + ": " + averr(ret);
		return false;
	}
	struct InputCloser {
		AVFormatContext **ctx;
		~InputCloser() { avformat_close_input(ctx); }
	} closeIn{&in};

	if ((ret = avformat_find_stream_info(in, nullptr)) < 0) {
		error = "could not read " + item.input + ": " + averr(ret);
		return false;
	}

	const int vidx = av_find_best_stream(in, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
	const int aidx = av_find_best_stream(in, AVMEDIA_TYPE_AUDIO, -1, vidx, nullptr, 0);
	if (vidx < 0) {
		error = item.input + " has no video";
		return false;
	}

	std::error_code ec;
	fs::create_directories(fs::u8path(dirNameOf(item.output)), ec);

	OutputFile out;
	ret = avformat_alloc_output_context2(&out.ctx, nullptr, "mp4", item.output.c_str());
	if (ret < 0 || !out.ctx) {
		error = "could not create MP4 muxer: " + averr(ret);
		return false;
	}

	std::vector<int> mapping(in->nb_streams, -1);
	for (int idx : {vidx, aidx}) {
		if (idx < 0)
			continue;
		AVStream *ist = in->streams[idx];
		AVStream *ost = avformat_new_stream(out.ctx, nullptr);
		if (!ost || avcodec_parameters_copy(ost->codecpar, ist->codecpar) < 0) {
			error = "could not copy stream parameters";
			return false;
		}
		ost->codecpar->codec_tag = 0;
		if (ost->codecpar->codec_id == AV_CODEC_ID_HEVC)
			ost->codecpar->codec_tag = MKTAG('h', 'v', 'c', '1');
		ost->time_base = ist->time_base;
		ost->avg_frame_rate = ist->avg_frame_rate;
		mapping[idx] = ost->index;
	}

	ret = avio_open(&out.ctx->pb, item.output.c_str(), AVIO_FLAG_WRITE);
	if (ret < 0) {
		error = "could not create " + item.output + ": " + averr(ret);
		return false;
	}
	AVDictionary *opts = nullptr;
	av_dict_set(&opts, "movflags", "+faststart", 0);
	ret = avformat_write_header(out.ctx, &opts);
	av_dict_free(&opts);
	if (ret < 0) {
		error = "could not write MP4 header: " + averr(ret);
		return false;
	}
	out.headerWritten = true;

	AVStream *vst = in->streams[vidx];
	const int64_t originUs =
		vst->start_time != AV_NOPTS_VALUE ? av_rescale_q(vst->start_time, vst->time_base, kMicros) : 0;
	const int64_t startUs = originUs + (int64_t)(item.start * 1e6);
	const bool bounded = item.end > item.start;
	const int64_t endUs = originUs + (int64_t)(item.end * 1e6);

	if (item.start > 0)
		av_seek_frame(in, vidx, av_rescale_q(startUs, kMicros, vst->time_base), AVSEEK_FLAG_BACKWARD);

	AVPacket *pkt = av_packet_alloc();
	int64_t offsetUs = AV_NOPTS_VALUE;
	bool videoDone = false;
	bool audioDone = aidx < 0;
	while (!(videoDone && audioDone) && av_read_frame(in, pkt) >= 0) {
		const int target = pkt->stream_index < (int)mapping.size() ? mapping[pkt->stream_index] : -1;
		if (target < 0 || (pkt->pts == AV_NOPTS_VALUE && pkt->dts == AV_NOPTS_VALUE)) {
			av_packet_unref(pkt);
			continue;
		}

		AVStream *ist = in->streams[pkt->stream_index];
		AVStream *ost = out.ctx->streams[target];
		const bool isVideo = pkt->stream_index == vidx;
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

		pkt->pts = av_rescale_q(ptsUs - offsetUs, kMicros, ost->time_base);
		pkt->dts = av_rescale_q(dtsUs - offsetUs, kMicros, ost->time_base);
		pkt->duration = av_rescale_q(pkt->duration, ist->time_base, ost->time_base);
		pkt->pos = -1;
		pkt->stream_index = target;
		ret = av_interleaved_write_frame(out.ctx, pkt);
		if (ret < 0) {
			av_packet_free(&pkt);
			error = "could not write " + item.output + ": " + averr(ret);
			return false;
		}
	}
	av_packet_free(&pkt);

	return out.finish(error);
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
				if (exportClip(item, err)) {
					written++;
				} else {
					removeQuietly(item.output);
					errors += err + "\n";
				}
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
