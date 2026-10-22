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
#include "cue-dock.hpp"
#include "ffmpeg-compat.hpp"
#include "rc-common.hpp"
#include "replay-controller.hpp"
#include "replay-player.hpp"
#include "websocket-api.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QMainWindow>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

static rc::ReplayController *controller = nullptr;

MODULE_EXPORT const char *obs_module_name(void)
{
	return "Replay Cue";
}

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Multi-angle instant replay: per-camera replay buffers, a replay cue list, slow motion and "
	       "frame-synced angle switching.";
}

bool obs_module_load(void)
{
	std::string details;
	const bool ffmpegOk = rc::ffmpegAbiCompatible(details);
	rc::setFfmpegReady(ffmpegOk);
	if (!ffmpegOk)
		obs_log(LOG_ERROR, "FFmpeg version mismatch (%s); recording and playback are disabled",
			details.c_str());

	rc::registerCaptureOutputs();
	rc::registerPlayerSource();

	controller = new rc::ReplayController();
	auto *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	auto *dock = new rc::CueDock(controller, mainWindow);
	obs_frontend_add_dock_by_id("replay-cue", obs_module_text("Dock.Title"), dock);

	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_post_load(void)
{
	rc::registerWebsocketVendor(controller);
}

void obs_module_unload(void)
{
	rc::unregisterWebsocketVendor();
	if (controller) {
		controller->shutdown();
		delete controller;
		controller = nullptr;
	}
	obs_log(LOG_INFO, "plugin unloaded");
}
