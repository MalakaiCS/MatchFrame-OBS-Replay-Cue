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

#include "replay-event.hpp"
#include "ffmpeg-compat.hpp"
#include "rc-common.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <set>
#include <system_error>

namespace fs = std::filesystem;

namespace rc {

const char *kSidecarExtension = ".rcue.json";

namespace {

fs::path toPath(const std::string &utf8)
{
	return fs::u8path(utf8);
}

std::string fromPath(const fs::path &path)
{
	return path.u8string();
}

std::string normalizedKey(const std::string &utf8)
{
	std::string key = fromPath(toPath(utf8).lexically_normal());
	std::replace(key.begin(), key.end(), '\\', '/');
	return key;
}

bool endsWith(const std::string &s, const std::string &suffix)
{
	return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool isAbsolute(const std::string &path)
{
	if (path.empty())
		return false;
	if (path[0] == '/' || path[0] == '\\')
		return true;
	return path.size() > 1 && path[1] == ':';
}

int64_t fileMtime(const fs::path &path)
{
	std::error_code ec;
	auto ftime = fs::last_write_time(path, ec);
	if (ec)
		return 0;
	using namespace std::chrono;
	auto sys =
		time_point_cast<system_clock::duration>(ftime - fs::file_time_type::clock::now() + system_clock::now());
	return duration_cast<seconds>(sys.time_since_epoch()).count();
}

/* "cam1", "camera2", "angle3", "iso4"... or one of the configured labels */
bool looksLikeAngle(const std::string &norm, const std::set<std::string> &labels)
{
	if (norm.empty())
		return false;
	if (labels.count(norm))
		return true;
	static const char *prefixes[] = {"camera", "cam", "angle", "ang", "view", "feed", "iso"};
	for (const char *prefix : prefixes) {
		size_t len = strlen(prefix);
		if (norm.size() <= len || norm.size() > len + 2 || norm.compare(0, len, prefix) != 0)
			continue;
		bool digits =
			std::all_of(norm.begin() + (long)len, norm.end(), [](char c) { return c >= '0' && c <= '9'; });
		if (digits)
			return true;
	}
	return false;
}

/* Splits "Goal_Cam_2" into the event key "Goal" and the angle "Cam_2". */
bool splitAngleFromStem(const std::string &stem, const std::set<std::string> &labels, std::string &key,
			std::string &angle)
{
	std::vector<std::string> parts;
	std::string cur;
	for (char c : stem) {
		if (c == '_' || c == '-' || c == ' ' || c == '.') {
			if (!cur.empty())
				parts.push_back(cur);
			cur.clear();
		} else {
			cur.push_back(c);
		}
	}
	if (!cur.empty())
		parts.push_back(cur);
	if (parts.size() < 2)
		return false;

	auto join = [&](size_t skipFrom, size_t skipCount) {
		std::string out;
		for (size_t i = 0; i < parts.size(); i++) {
			if (i >= skipFrom && i < skipFrom + skipCount)
				continue;
			if (!out.empty())
				out += "_";
			out += parts[i];
		}
		return out;
	};

	/* prefer the last occurrence: "2026-10-22_Goal_Cam1" */
	for (size_t i = parts.size(); i-- > 0;) {
		if (i + 1 < parts.size() && looksLikeAngle(normalizeTag(parts[i] + parts[i + 1]), labels)) {
			angle = parts[i] + " " + parts[i + 1];
			key = join(i, 2);
			return !key.empty();
		}
		if (looksLikeAngle(normalizeTag(parts[i]), labels)) {
			angle = parts[i];
			key = join(i, 1);
			return !key.empty();
		}
	}
	return false;
}

struct LooseFile {
	std::string path;
	std::string key;
	std::string angle;
	int64_t mtime = 0;
};

int labelRank(const std::string &angle, const std::vector<std::string> &labels)
{
	std::string norm = normalizeTag(angle);
	for (size_t i = 0; i < labels.size(); i++)
		if (normalizeTag(labels[i]) == norm)
			return (int)i;
	return (int)labels.size();
}

} // namespace

double ReplayEvent::inOffset(size_t angle) const
{
	if (angle >= angles.size() || !inUsec || !angles[angle].firstFrameUsec)
		return 0;
	double offset = (double)(inUsec - angles[angle].firstFrameUsec) / 1e6;
	return offset > 0 ? offset : 0;
}

std::vector<std::string> ReplayEvent::angleLabels() const
{
	std::vector<std::string> labels;
	for (const AngleClip &clip : angles)
		labels.push_back(clip.label);
	return labels;
}

bool isMediaFile(const std::string &path)
{
	static const std::set<std::string> exts = {"mp4", "mov",  "m4v", "mkv", "ts",  "mts", "m2ts",
						   "flv", "webm", "avi", "mxf", "hls", "fmp4"};
	std::string name = fileNameOf(path);
	if (name.empty() || name[0] == '.')
		return false;
	return exts.count(extensionOf(path)) > 0;
}

obs_data_t *eventToData(const ReplayEvent &event)
{
	obs_data_t *data = obs_data_create();
	obs_data_set_string(data, "id", event.id.c_str());
	obs_data_set_string(data, "name", event.name.c_str());
	obs_data_set_string(data, "tag", event.tag.c_str());
	obs_data_set_int(data, "number", event.number);
	obs_data_set_int(data, "created", event.created);
	obs_data_set_double(data, "duration", event.duration);
	obs_data_set_bool(data, "imported", event.imported);
	obs_data_set_bool(data, "favorite", event.favorite);
	obs_data_set_bool(data, "highlight", event.highlight);
	obs_data_set_int(data, "period", event.period);
	obs_data_set_string(data, "match", event.match.c_str());

	obs_data_array_t *angles = obs_data_array_create();
	for (const AngleClip &clip : event.angles) {
		obs_data_t *a = obs_data_create();
		obs_data_set_string(a, "label", clip.label.c_str());
		obs_data_set_string(a, "file", clip.file.c_str());
		obs_data_set_double(a, "duration", clip.duration);
		obs_data_array_push_back(angles, a);
		obs_data_release(a);
	}
	obs_data_set_array(data, "angles", angles);
	obs_data_array_release(angles);
	return data;
}

bool saveSidecar(const ReplayEvent &event, std::string &error)
{
	if (event.sidecar.empty()) {
		error = "no sidecar path";
		return false;
	}

	obs_data_t *data = eventToData(event);
	obs_data_set_string(data, "format", "replay-cue-event");
	obs_data_set_int(data, "version", 1);
	obs_data_set_int(data, "in_usec", event.inUsec);
	obs_data_set_int(data, "out_usec", event.outUsec);

	/* store files next to the sidecar as relative paths so the folder can be
	 * moved or copied to another machine */
	std::string dir = normalizedKey(dirNameOf(event.sidecar)) + "/";
	obs_data_array_t *angles = obs_data_get_array(data, "angles");
	for (size_t i = 0; i < event.angles.size(); i++) {
		obs_data_t *a = obs_data_array_item(angles, i);
		std::string file = normalizedKey(event.angles[i].file);
		if (file.compare(0, dir.size(), dir) == 0)
			file = file.substr(dir.size());
		obs_data_set_string(a, "file", file.c_str());
		obs_data_set_int(a, "first_frame_usec", event.angles[i].firstFrameUsec);
		obs_data_release(a);
	}
	obs_data_array_release(angles);

	bool ok = obs_data_save_json_pretty_safe(data, event.sidecar.c_str(), ".tmp", nullptr);
	obs_data_release(data);
	if (!ok)
		error = "could not write " + event.sidecar;
	return ok;
}

bool loadSidecar(const std::string &path, ReplayEvent &event)
{
	obs_data_t *data = obs_data_create_from_json_file(path.c_str());
	if (!data)
		return false;

	if (strcmp(obs_data_get_string(data, "format"), "replay-cue-event") != 0) {
		obs_data_release(data);
		return false;
	}

	event = ReplayEvent();
	event.sidecar = path;
	event.id = obs_data_get_string(data, "id");
	event.name = obs_data_get_string(data, "name");
	event.tag = obs_data_get_string(data, "tag");
	event.number = (int)obs_data_get_int(data, "number");
	event.created = obs_data_get_int(data, "created");
	event.inUsec = obs_data_get_int(data, "in_usec");
	event.outUsec = obs_data_get_int(data, "out_usec");
	event.duration = obs_data_get_double(data, "duration");
	event.imported = obs_data_get_bool(data, "imported");
	event.favorite = obs_data_get_bool(data, "favorite");
	event.highlight = obs_data_get_bool(data, "highlight");
	event.period = (int)obs_data_get_int(data, "period");
	event.match = obs_data_get_string(data, "match");
	if (event.id.empty())
		event.id = "rc:" + stemOf(path);

	std::string dir = dirNameOf(path);
	obs_data_array_t *angles = obs_data_get_array(data, "angles");
	size_t count = obs_data_array_count(angles);
	for (size_t i = 0; i < count; i++) {
		obs_data_t *a = obs_data_array_item(angles, i);
		AngleClip clip;
		clip.label = obs_data_get_string(a, "label");
		clip.file = obs_data_get_string(a, "file");
		if (!isAbsolute(clip.file))
			clip.file = joinPath(dir, clip.file);
		clip.firstFrameUsec = obs_data_get_int(a, "first_frame_usec");
		clip.duration = obs_data_get_double(a, "duration");
		obs_data_release(a);
		event.angles.push_back(clip);
	}
	obs_data_array_release(angles);
	obs_data_release(data);
	return !event.angles.empty();
}

double probeDuration(const std::string &path)
{
	if (!ffmpegReady())
		return 0;

	AVFormatContext *fmt = nullptr;
	if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0)
		return 0;

	double duration = 0;
	if (avformat_find_stream_info(fmt, nullptr) >= 0) {
		if (fmt->duration != AV_NOPTS_VALUE && fmt->duration > 0) {
			duration = (double)fmt->duration / AV_TIME_BASE;
		} else {
			for (unsigned i = 0; i < fmt->nb_streams; i++) {
				AVStream *st = fmt->streams[i];
				if (st->duration != AV_NOPTS_VALUE && st->duration > 0)
					duration = std::max(duration, st->duration * av_q2d(st->time_base));
			}
		}
	}
	avformat_close_input(&fmt);
	return duration;
}

double ProbeCache::duration(const std::string &path)
{
	std::error_code ec;
	fs::path p = toPath(path);
	int64_t size = (int64_t)fs::file_size(p, ec);
	if (ec)
		return 0;
	int64_t mtime = fileMtime(p);

	auto it = entries.find(path);
	if (it != entries.end() && it->second.size == size && it->second.mtime == mtime)
		return it->second.duration;

	Entry entry;
	entry.size = size;
	entry.mtime = mtime;
	entry.duration = probeDuration(path);
	entries[path] = entry;
	return entry.duration;
}

std::vector<ReplayEvent> scanReplayFolder(const std::string &folder, const std::vector<std::string> &angleLabels,
					  ProbeCache &cache)
{
	std::vector<ReplayEvent> events;
	std::error_code ec;
	fs::path root = toPath(folder);
	if (folder.empty() || !fs::is_directory(root, ec))
		return events;

	std::set<std::string> labels;
	for (const std::string &label : angleLabels)
		labels.insert(normalizeTag(label));

	/* 1. sidecars written by Replay Cue */
	std::set<std::string> referenced;
	for (const auto &entry : fs::directory_iterator(root, fs::directory_options::skip_permission_denied, ec)) {
		std::string path = fromPath(entry.path());
		if (!entry.is_regular_file(ec) || !endsWith(path, kSidecarExtension))
			continue;
		ReplayEvent event;
		if (!loadSidecar(path, event))
			continue;
		for (const AngleClip &clip : event.angles)
			referenced.insert(normalizedKey(clip.file));
		events.push_back(std::move(event));
	}

	/* 2. loose media files, grouped into events */
	std::vector<LooseFile> loose;
	auto consider = [&](const fs::path &p, const std::string &folderAngle) {
		std::string path = fromPath(p);
		if (!isMediaFile(path) || referenced.count(normalizedKey(path)))
			return;
		LooseFile f;
		f.path = path;
		f.mtime = fileMtime(p);
		std::string stem = stemOf(path);
		if (!folderAngle.empty()) {
			f.key = stem;
			f.angle = folderAngle;
		} else if (!splitAngleFromStem(stem, labels, f.key, f.angle)) {
			f.key = stem;
			f.angle = "Main";
		}
		loose.push_back(f);
	};

	for (const auto &entry : fs::directory_iterator(root, fs::directory_options::skip_permission_denied, ec)) {
		if (entry.is_regular_file(ec)) {
			consider(entry.path(), std::string());
		} else if (entry.is_directory(ec)) {
			std::string dirName = fromPath(entry.path().filename());
			if (!looksLikeAngle(normalizeTag(dirName), labels))
				continue;
			std::error_code ec2;
			for (const auto &sub :
			     fs::directory_iterator(entry.path(), fs::directory_options::skip_permission_denied, ec2))
				if (sub.is_regular_file(ec2))
					consider(sub.path(), dirName);
		}
	}

	std::map<std::string, std::vector<LooseFile>> groups;
	for (const LooseFile &f : loose)
		groups[f.key].push_back(f);

	for (auto &[key, files] : groups) {
		std::sort(files.begin(), files.end(), [&](const LooseFile &a, const LooseFile &b) {
			int ra = labelRank(a.angle, angleLabels), rb = labelRank(b.angle, angleLabels);
			return ra != rb ? ra < rb : a.angle < b.angle;
		});

		ReplayEvent event;
		event.id = "file:" + normalizedKey(joinPath(folder, key));
		event.name = key;
		event.imported = true;
		event.created = files.front().mtime;
		std::set<std::string> used;
		for (const LooseFile &f : files) {
			AngleClip clip;
			clip.label = f.angle;
			for (int n = 2; used.count(clip.label); n++)
				clip.label = f.angle + " (" + std::to_string(n) + ")";
			used.insert(clip.label);
			clip.file = f.path;
			clip.duration = cache.duration(f.path);
			event.created = std::min(event.created, f.mtime);
			event.duration = std::max(event.duration, clip.duration);
			event.angles.push_back(clip);
			if (event.angles.size() >= (size_t)kMaxAngles)
				break;
		}
		events.push_back(std::move(event));
	}

	std::sort(events.begin(), events.end(), [](const ReplayEvent &a, const ReplayEvent &b) {
		if (a.created != b.created)
			return a.created < b.created;
		if (a.number != b.number)
			return a.number < b.number;
		return a.name < b.name;
	});
	return events;
}

} // namespace rc
