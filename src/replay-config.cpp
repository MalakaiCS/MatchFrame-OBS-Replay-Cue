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

#include "replay-config.hpp"
#include "rc-common.hpp"

#include <QDir>
#include <QStandardPaths>

#include <algorithm>

namespace rc {

std::string Config::configPath()
{
	char *path = obs_module_config_path("config.json");
	std::string out = path ? path : "";
	bfree(path);
	return out;
}

std::string Config::defaultFolder()
{
	QString movies = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
	if (movies.isEmpty())
		movies = QDir::homePath();
	return QDir(movies).filePath("Replays").toStdString();
}

static std::string getString(obs_data_t *data, const char *name, const std::string &fallback)
{
	if (!obs_data_has_user_value(data, name))
		return fallback;
	return obs_data_get_string(data, name);
}

static int getInt(obs_data_t *data, const char *name, int fallback)
{
	return obs_data_has_user_value(data, name) ? (int)obs_data_get_int(data, name) : fallback;
}

static bool getBool(obs_data_t *data, const char *name, bool fallback)
{
	return obs_data_has_user_value(data, name) ? obs_data_get_bool(data, name) : fallback;
}

static double getDouble(obs_data_t *data, const char *name, double fallback)
{
	return obs_data_has_user_value(data, name) ? obs_data_get_double(data, name) : fallback;
}

void Config::load()
{
	*this = Config();
	folder = defaultFolder();
	exportFolder = QDir(QString::fromStdString(folder)).filePath("Exports").toStdString();
	tags = {"Goal", "Foul", "Save", "Highlight"};

	std::string path = configPath();
	obs_data_t *data = path.empty() ? nullptr : obs_data_create_from_json_file_safe(path.c_str(), "bak");
	if (!data)
		return;

	obs_data_array_t *arr = obs_data_get_array(data, "angles");
	for (size_t i = 0, n = obs_data_array_count(arr); i < n && angles.size() < (size_t)kMaxAngles; i++) {
		obs_data_t *a = obs_data_array_item(arr, i);
		AngleSetting setting;
		setting.label = obs_data_get_string(a, "label");
		setting.source = obs_data_get_string(a, "source");
		setting.enabled = getBool(a, "enabled", true);
		if (setting.label.empty())
			setting.label = "Cam " + std::to_string(angles.size() + 1);
		angles.push_back(setting);
		obs_data_release(a);
	}
	obs_data_array_release(arr);

	armOnStartup = getBool(data, "arm_on_startup", armOnStartup);
	bufferSeconds = std::clamp(getInt(data, "buffer_seconds", bufferSeconds), 10, 3600);
	encoderId = getString(data, "encoder", encoderId);
	bitrateKbps = std::clamp(getInt(data, "bitrate", bitrateKbps), 1000, 200000);
	keyintSec = std::clamp(getInt(data, "keyint_sec", keyintSec), 1, 10);
	maxHeight = std::max(0, getInt(data, "max_height", maxHeight));
	audioTrack = std::clamp(getInt(data, "audio_track", audioTrack), 0, 6);

	folder = getString(data, "folder", folder);
	watchFolder = getBool(data, "watch_folder", watchFolder);
	instantSeconds = std::clamp(getInt(data, "instant_seconds", instantSeconds), 1, 600);
	tailMs = std::clamp(getInt(data, "tail_ms", tailMs), 0, 5000);
	markLatencyMs = std::clamp(getInt(data, "mark_latency_ms", markLatencyMs), 0, 5000);
	importReplayBuffer = getBool(data, "import_replay_buffer", importReplayBuffer);
	exportFolder = getString(data, "export_folder", exportFolder);

	if (obs_data_has_user_value(data, "tags")) {
		tags.clear();
		obs_data_array_t *tagArr = obs_data_get_array(data, "tags");
		for (size_t i = 0, n = obs_data_array_count(tagArr); i < n; i++) {
			obs_data_t *t = obs_data_array_item(tagArr, i);
			std::string tag = obs_data_get_string(t, "name");
			if (!tag.empty())
				tags.push_back(tag);
			obs_data_release(t);
		}
		obs_data_array_release(tagArr);
	}

	sceneName = getString(data, "scene_name", sceneName);
	playerName = getString(data, "player_name", playerName);
	replayTransition = getString(data, "replay_transition", replayTransition);
	returnTransition = getString(data, "return_transition", returnTransition);
	liveScene = getString(data, "live_scene", liveScene);
	defaultSpeed = std::clamp(getDouble(data, "default_speed", defaultSpeed), 0.05, 2.0);
	autoPlayNext = getBool(data, "auto_play_next", autoPlayNext);
	cycleAngles = getBool(data, "cycle_angles", cycleAngles);
	returnToLive = getBool(data, "return_to_live", returnToLive);
	playAudio = getBool(data, "play_audio", playAudio);
	hwDecode = getBool(data, "hw_decode", hwDecode);
	startCuedOnLive = getBool(data, "start_cued_on_live", startCuedOnLive);

	if (sceneName.empty())
		sceneName = "Replay";
	if (playerName.empty())
		playerName = "Replay Cue Player";

	obs_data_release(data);
}

void Config::save() const
{
	std::string path = configPath();
	if (path.empty())
		return;

	char *dir = obs_module_config_path("");
	if (dir) {
		os_mkdirs(dir);
		bfree(dir);
	}

	/* keep keys this version does not know about (e.g. hotkeys) */
	obs_data_t *data = obs_data_create_from_json_file_safe(path.c_str(), "bak");
	if (!data)
		data = obs_data_create();

	obs_data_array_t *arr = obs_data_array_create();
	for (const AngleSetting &angle : angles) {
		obs_data_t *a = obs_data_create();
		obs_data_set_string(a, "label", angle.label.c_str());
		obs_data_set_string(a, "source", angle.source.c_str());
		obs_data_set_bool(a, "enabled", angle.enabled);
		obs_data_array_push_back(arr, a);
		obs_data_release(a);
	}
	obs_data_set_array(data, "angles", arr);
	obs_data_array_release(arr);

	obs_data_set_bool(data, "arm_on_startup", armOnStartup);
	obs_data_set_int(data, "buffer_seconds", bufferSeconds);
	obs_data_set_string(data, "encoder", encoderId.c_str());
	obs_data_set_int(data, "bitrate", bitrateKbps);
	obs_data_set_int(data, "keyint_sec", keyintSec);
	obs_data_set_int(data, "max_height", maxHeight);
	obs_data_set_int(data, "audio_track", audioTrack);

	obs_data_set_string(data, "folder", folder.c_str());
	obs_data_set_bool(data, "watch_folder", watchFolder);
	obs_data_set_int(data, "instant_seconds", instantSeconds);
	obs_data_set_int(data, "tail_ms", tailMs);
	obs_data_set_int(data, "mark_latency_ms", markLatencyMs);
	obs_data_set_bool(data, "import_replay_buffer", importReplayBuffer);
	obs_data_set_string(data, "export_folder", exportFolder.c_str());

	obs_data_array_t *tagArr = obs_data_array_create();
	for (const std::string &tag : tags) {
		obs_data_t *t = obs_data_create();
		obs_data_set_string(t, "name", tag.c_str());
		obs_data_array_push_back(tagArr, t);
		obs_data_release(t);
	}
	obs_data_set_array(data, "tags", tagArr);
	obs_data_array_release(tagArr);

	obs_data_set_string(data, "scene_name", sceneName.c_str());
	obs_data_set_string(data, "player_name", playerName.c_str());
	obs_data_set_string(data, "replay_transition", replayTransition.c_str());
	obs_data_set_string(data, "return_transition", returnTransition.c_str());
	obs_data_set_string(data, "live_scene", liveScene.c_str());
	obs_data_set_double(data, "default_speed", defaultSpeed);
	obs_data_set_bool(data, "auto_play_next", autoPlayNext);
	obs_data_set_bool(data, "cycle_angles", cycleAngles);
	obs_data_set_bool(data, "return_to_live", returnToLive);
	obs_data_set_bool(data, "play_audio", playAudio);
	obs_data_set_bool(data, "hw_decode", hwDecode);
	obs_data_set_bool(data, "start_cued_on_live", startCuedOnLive);

	obs_data_save_json_pretty_safe(data, path.c_str(), "tmp", "bak");
	obs_data_release(data);
}

std::vector<AngleSpec> Config::enabledAngles() const
{
	std::vector<AngleSpec> out;
	for (const AngleSetting &angle : angles)
		if (angle.enabled && !angle.source.empty())
			out.push_back({angle.label, angle.source});
	return out;
}

CaptureSettings Config::captureSettings() const
{
	CaptureSettings s;
	s.encoderId = encoderId;
	s.bitrateKbps = bitrateKbps;
	s.keyintSec = keyintSec;
	s.maxHeight = maxHeight;
	s.bufferSeconds = bufferSeconds;
	s.audioTrack = audioTrack;
	return s;
}

std::vector<std::string> Config::angleLabels() const
{
	std::vector<std::string> out;
	for (const AngleSetting &angle : angles)
		out.push_back(angle.label);
	return out;
}

} // namespace rc
