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

#include "websocket-api.hpp"
#include "cue-dock.hpp"
#include "rc-common.hpp"
#include "replay-controller.hpp"
#include "third-party/obs-websocket-api.h"

#include <QMetaObject>

#include <atomic>
#include <functional>
#include <string>

namespace rc {

namespace {

obs_websocket_vendor vendor = nullptr;
std::atomic<ReplayController *> controller{nullptr};

using Action = std::function<void(ReplayController &)>;

/* Requests arrive on the websocket thread: hand the work to the UI thread. */
void queueAction(obs_data_t *response, Action action)
{
	ReplayController *c = controller.load();
	if (!c) {
		obs_data_set_bool(response, "ok", false);
		obs_data_set_string(response, "error", "Replay Cue is shutting down");
		return;
	}
	QMetaObject::invokeMethod(c, [c, action]() { action(*c); }, Qt::QueuedConnection);
	obs_data_set_bool(response, "ok", true);
}

void fail(obs_data_t *response, const char *error)
{
	obs_data_set_bool(response, "ok", false);
	obs_data_set_string(response, "error", error);
}

/* "id", or a 1-based "index" into the event list; empty means "selected". */
bool resolveEventId(obs_data_t *request, obs_data_t *response, std::string &id)
{
	id.clear();
	if (obs_data_has_user_value(request, "id")) {
		id = obs_data_get_string(request, "id");
		return true;
	}
	if (!obs_data_has_user_value(request, "index"))
		return true;

	ReplayController *c = controller.load();
	if (!c) {
		fail(response, "Replay Cue is shutting down");
		return false;
	}
	OBSDataAutoRelease snap = c->eventsSnapshot();
	OBSDataArrayAutoRelease events = obs_data_get_array(snap, "events");
	const long long count = (long long)obs_data_array_count(events);
	long long index = obs_data_get_int(request, "index");
	if (index < 0)
		index = count + 1 + index; /* -1 = latest */
	if (index < 1 || index > count) {
		fail(response, "event index out of range");
		return false;
	}
	OBSDataAutoRelease item = obs_data_array_item(events, (size_t)(index - 1));
	id = obs_data_get_string(item, "id");
	return true;
}

#define SIMPLE(fn_name, body)                                                       \
	void fn_name(obs_data_t *, obs_data_t *response, void *)                    \
	{                                                                           \
		queueAction(response, [](ReplayController &c) { body; });         \
	}

SIMPLE(reqArm, c.arm())
SIMPLE(reqDisarm, c.disarm())
SIMPLE(reqToggleArm, c.toggleArm())
SIMPLE(reqMarkIn, c.markIn())
SIMPLE(reqMarkOut, c.markOut())
SIMPLE(reqClearMarks, c.clearMarks())
SIMPLE(reqPlaySelected, c.playSelected())
SIMPLE(reqPlayLatest, c.playLatest())
SIMPLE(reqPlayNext, c.playNextEvent())
SIMPLE(reqPlayPrevious, c.playPreviousEvent())
SIMPLE(reqPlayPause, c.togglePause())
SIMPLE(reqPause, c.pause())
SIMPLE(reqResume, c.resume())
SIMPLE(reqRestart, c.restart())
SIMPLE(reqReturnToLive, c.returnToLive())
SIMPLE(reqNextAngle, c.nextAngle())
SIMPLE(reqPrevAngle, c.prevAngle())
SIMPLE(reqRefresh, c.refreshEvents())

#undef SIMPLE

void reqGetStatus(obs_data_t *, obs_data_t *response, void *)
{
	ReplayController *c = controller.load();
	if (!c)
		return fail(response, "Replay Cue is shutting down");
	OBSDataAutoRelease snap = c->statusSnapshot();
	obs_data_apply(response, snap);
	obs_data_set_bool(response, "ok", true);
}

void reqGetEvents(obs_data_t *, obs_data_t *response, void *)
{
	ReplayController *c = controller.load();
	if (!c)
		return fail(response, "Replay Cue is shutting down");
	OBSDataAutoRelease snap = c->eventsSnapshot();
	obs_data_apply(response, snap);
	obs_data_set_bool(response, "ok", true);
}

void reqInstantReplay(obs_data_t *request, obs_data_t *response, void *)
{
	const int seconds = (int)obs_data_get_int(request, "seconds");
	const bool play = obs_data_get_bool(request, "play");
	queueAction(response, [seconds, play](ReplayController &c) { c.instantReplay(play, seconds); });
}

void reqSelectEvent(obs_data_t *request, obs_data_t *response, void *)
{
	std::string id;
	if (!resolveEventId(request, response, id))
		return;
	if (id.empty())
		return fail(response, "pass \"id\" or \"index\"");
	queueAction(response, [id](ReplayController &c) { c.selectEvent(QString::fromStdString(id)); });
}

void reqCueEvent(obs_data_t *request, obs_data_t *response, void *)
{
	std::string id;
	if (!resolveEventId(request, response, id))
		return;
	queueAction(response, [id](ReplayController &c) {
		if (id.empty())
			c.cueSelected();
		else
			c.cueEvent(QString::fromStdString(id));
	});
}

void reqPlayEvent(obs_data_t *request, obs_data_t *response, void *)
{
	std::string id;
	if (!resolveEventId(request, response, id))
		return;
	const int angle = (int)obs_data_get_int(request, "angle") - 1;
	const bool hasSpeed = obs_data_has_user_value(request, "speed");
	const double speed = obs_data_get_double(request, "speed");
	queueAction(response, [id, angle, hasSpeed, speed](ReplayController &c) {
		if (hasSpeed)
			c.setSpeed(speed);
		if (id.empty())
			c.playSelected();
		else
			c.playEvent(QString::fromStdString(id), angle);
	});
}

void reqPlayQueue(obs_data_t *request, obs_data_t *response, void *)
{
	OBSDataArrayAutoRelease ids = obs_data_get_array(request, "ids");
	QStringList list;
	for (size_t i = 0, n = obs_data_array_count(ids); i < n; i++) {
		OBSDataAutoRelease item = obs_data_array_item(ids, i);
		list << QString::fromUtf8(obs_data_get_string(item, "id"));
	}
	if (list.isEmpty())
		return fail(response, "pass \"ids\": [{\"id\": ...}, ...]");
	queueAction(response, [list](ReplayController &c) { c.playQueue(list); });
}

void reqSetAngle(obs_data_t *request, obs_data_t *response, void *)
{
	if (obs_data_has_user_value(request, "label")) {
		const std::string label = normalizeTag(obs_data_get_string(request, "label"));
		queueAction(response, [label](ReplayController &c) {
			const auto labels = c.activeAngleLabels();
			for (size_t i = 0; i < labels.size(); i++)
				if (normalizeTag(labels[i]) == label)
					c.setAngle((int)i);
		});
		return;
	}
	const int angle = (int)obs_data_get_int(request, "angle") - 1;
	if (angle < 0)
		return fail(response, "pass \"angle\" (1-based) or \"label\"");
	queueAction(response, [angle](ReplayController &c) { c.setAngle(angle); });
}

void reqSetSpeed(obs_data_t *request, obs_data_t *response, void *)
{
	double speed = obs_data_get_double(request, "speed");
	if (obs_data_has_user_value(request, "percent"))
		speed = obs_data_get_double(request, "percent") / 100.0;
	if (speed <= 0)
		return fail(response, "pass \"speed\" (0.05-2.0) or \"percent\"");
	queueAction(response, [speed](ReplayController &c) { c.setSpeed(speed); });
}

void reqSeek(obs_data_t *request, obs_data_t *response, void *)
{
	const double position = obs_data_get_double(request, "position");
	queueAction(response, [position](ReplayController &c) { c.seek(position); });
}

void reqStep(obs_data_t *request, obs_data_t *response, void *)
{
	int frames = (int)obs_data_get_int(request, "frames");
	if (frames == 0)
		frames = 1;
	queueAction(response, [frames](ReplayController &c) { c.step(frames); });
}

void reqSetOptions(obs_data_t *request, obs_data_t *response, void *)
{
	const bool hasNext = obs_data_has_user_value(request, "autoPlayNext");
	const bool hasCycle = obs_data_has_user_value(request, "cycleAngles");
	const bool hasReturn = obs_data_has_user_value(request, "returnToLive");
	const bool next = obs_data_get_bool(request, "autoPlayNext");
	const bool cycle = obs_data_get_bool(request, "cycleAngles");
	const bool ret = obs_data_get_bool(request, "returnToLive");
	queueAction(response, [=](ReplayController &c) {
		if (hasNext)
			c.setAutoPlayNext(next);
		if (hasCycle)
			c.setCycleAngles(cycle);
		if (hasReturn)
			c.setReturnToLive(ret);
	});
}

void reqDeleteEvent(obs_data_t *request, obs_data_t *response, void *)
{
	std::string id;
	if (!resolveEventId(request, response, id))
		return;
	if (id.empty())
		return fail(response, "pass \"id\" or \"index\"");
	queueAction(response, [id](ReplayController &c) { c.deleteEvents({QString::fromStdString(id)}); });
}

void reqRenameEvent(obs_data_t *request, obs_data_t *response, void *)
{
	std::string id;
	if (!resolveEventId(request, response, id))
		return;
	const QString name = QString::fromUtf8(obs_data_get_string(request, "name"));
	if (id.empty() || name.isEmpty())
		return fail(response, "pass \"id\" (or \"index\") and \"name\"");
	queueAction(response, [id, name](ReplayController &c) { c.renameEvent(QString::fromStdString(id), name); });
}

void reqSetEventTag(obs_data_t *request, obs_data_t *response, void *)
{
	std::string id;
	if (!resolveEventId(request, response, id))
		return;
	if (id.empty())
		return fail(response, "pass \"id\" or \"index\"");
	const QString tag = QString::fromUtf8(obs_data_get_string(request, "tag"));
	queueAction(response, [id, tag](ReplayController &c) { c.setEventTag(QString::fromStdString(id), tag); });
}

void reqToggleFavorite(obs_data_t *request, obs_data_t *response, void *)
{
	std::string id;
	if (!resolveEventId(request, response, id))
		return;
	if (id.empty())
		return fail(response, "pass \"id\" or \"index\"");
	queueAction(response, [id](ReplayController &c) { c.toggleFavorite(QString::fromStdString(id)); });
}

void reqExportEvent(obs_data_t *request, obs_data_t *response, void *)
{
	std::string id;
	if (!resolveEventId(request, response, id))
		return;
	const int angle = (int)obs_data_get_int(request, "angle") - 1;
	const bool trim = !obs_data_has_user_value(request, "trim") || obs_data_get_bool(request, "trim");
	queueAction(response, [id, angle, trim](ReplayController &c) {
		const std::string target = id.empty() ? c.selectedEventId() : id;
		c.exportEvent(QString::fromStdString(target), angle, trim);
	});
}

void reqSaveUiSnapshot(obs_data_t *request, obs_data_t *response, void *)
{
	const QString folder = QString::fromUtf8(obs_data_get_string(request, "folder"));
	if (folder.isEmpty())
		return fail(response, "pass \"folder\"");
	queueAction(response, [folder](ReplayController &c) { saveUiSnapshots(&c, folder); });
}

struct Request {
	const char *name;
	obs_websocket_request_callback_function fn;
};

const Request kRequests[] = {
	{"GetStatus", reqGetStatus},
	{"GetEvents", reqGetEvents},
	{"Arm", reqArm},
	{"Disarm", reqDisarm},
	{"ToggleArm", reqToggleArm},
	{"MarkIn", reqMarkIn},
	{"MarkOut", reqMarkOut},
	{"ClearMarks", reqClearMarks},
	{"InstantReplay", reqInstantReplay},
	{"SelectEvent", reqSelectEvent},
	{"CueEvent", reqCueEvent},
	{"PlayEvent", reqPlayEvent},
	{"PlaySelected", reqPlaySelected},
	{"PlayLatest", reqPlayLatest},
	{"PlayNext", reqPlayNext},
	{"PlayPrevious", reqPlayPrevious},
	{"PlayQueue", reqPlayQueue},
	{"PlayPause", reqPlayPause},
	{"Pause", reqPause},
	{"Resume", reqResume},
	{"Restart", reqRestart},
	{"ReturnToLive", reqReturnToLive},
	{"Stop", reqReturnToLive},
	{"SetAngle", reqSetAngle},
	{"NextAngle", reqNextAngle},
	{"PrevAngle", reqPrevAngle},
	{"SetSpeed", reqSetSpeed},
	{"Seek", reqSeek},
	{"Step", reqStep},
	{"SetOptions", reqSetOptions},
	{"DeleteEvent", reqDeleteEvent},
	{"RenameEvent", reqRenameEvent},
	{"SetEventTag", reqSetEventTag},
	{"ToggleFavorite", reqToggleFavorite},
	{"ExportEvent", reqExportEvent},
	{"RefreshEvents", reqRefresh},
	{"SaveUiSnapshot", reqSaveUiSnapshot},
};

} // namespace

void registerWebsocketVendor(ReplayController *c)
{
	controller.store(c);
	vendor = obs_websocket_register_vendor("ReplayCue");
	if (!vendor) {
		obs_log(LOG_INFO, "obs-websocket not available; remote control disabled");
		return;
	}
	for (const Request &r : kRequests)
		if (!obs_websocket_vendor_register_request(vendor, r.name, r.fn, nullptr))
			obs_log(LOG_WARNING, "could not register websocket request %s", r.name);
	obs_log(LOG_INFO, "obs-websocket vendor 'ReplayCue' registered (%zu requests)",
		sizeof(kRequests) / sizeof(kRequests[0]));
}

void unregisterWebsocketVendor()
{
	controller.store(nullptr);
}

void emitVendorEvent(const char *name, obs_data_t *data)
{
	if (vendor && controller.load())
		obs_websocket_vendor_emit_event(vendor, name, data);
}

} // namespace rc
