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
#include <optional>
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
SIMPLE(reqNextMatchPhase, c.nextMatchPhase())
SIMPLE(reqStartCleanFeed, c.startCleanFeed())
SIMPLE(reqStopCleanFeed, c.stopCleanFeed())

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
	const bool hasAuto = obs_data_has_user_value(request, "autoHighlights");
	const bool hasFade = obs_data_has_user_value(request, "highlightsFade");
	const bool hasAll = obs_data_has_user_value(request, "showAllMatches");
	const bool next = obs_data_get_bool(request, "autoPlayNext");
	const bool cycle = obs_data_get_bool(request, "cycleAngles");
	const bool ret = obs_data_get_bool(request, "returnToLive");
	const bool autoHighlights = obs_data_get_bool(request, "autoHighlights");
	const double fade = obs_data_get_double(request, "highlightsFade");
	const bool all = obs_data_get_bool(request, "showAllMatches");
	queueAction(response, [=](ReplayController &c) {
		if (hasNext)
			c.setAutoPlayNext(next);
		if (hasCycle)
			c.setCycleAngles(cycle);
		if (hasReturn)
			c.setReturnToLive(ret);
		if (hasAuto)
			c.setAutoHighlights(autoHighlights);
		if (hasFade)
			c.setHighlightsFade(fade);
		if (hasAll)
			c.setShowAllMatches(all);
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

void reqGetMatch(obs_data_t *, obs_data_t *response, void *)
{
	ReplayController *c = controller.load();
	if (!c)
		return fail(response, "Replay Cue is shutting down");
	OBSDataAutoRelease snap = c->matchSnapshot();
	obs_data_apply(response, snap);
	obs_data_set_bool(response, "ok", true);
}

void reqGetMatches(obs_data_t *, obs_data_t *response, void *)
{
	ReplayController *c = controller.load();
	if (!c)
		return fail(response, "Replay Cue is shutting down");
	OBSDataAutoRelease snap = c->matchSnapshot();
	OBSDataArrayAutoRelease matches = obs_data_get_array(snap, "matches");
	obs_data_set_array(response, "matches", matches);
	obs_data_set_string(response, "open", obs_data_get_string(snap, "match_id"));
	obs_data_set_bool(response, "ok", true);
}

void reqNewMatch(obs_data_t *request, obs_data_t *response, void *)
{
	const QString name = QString::fromUtf8(obs_data_get_string(request, "name"));
	const QString home = QString::fromUtf8(obs_data_get_string(request, "home"));
	const QString away = QString::fromUtf8(obs_data_get_string(request, "away"));
	queueAction(response, [name, home, away](ReplayController &c) { c.newMatch(name, home, away); });
}

/* name, home and away: the ones not given stay as they are */
void reqSetMatchInfo(obs_data_t *request, obs_data_t *response, void *)
{
	auto field = [request](const char *key) -> std::optional<QString> {
		if (!obs_data_has_user_value(request, key))
			return std::nullopt;
		return QString::fromUtf8(obs_data_get_string(request, key));
	};
	const auto name = field("name"), home = field("home"), away = field("away");
	if (!name && !home && !away)
		return fail(response, "pass \"name\", \"home\" or \"away\"");
	queueAction(response, [name, home, away](ReplayController &c) {
		const MatchInfo &m = c.openMatch();
		c.editMatch(name.value_or(QString::fromStdString(m.name)),
			    home.value_or(QString::fromStdString(m.home)),
			    away.value_or(QString::fromStdString(m.away)));
	});
}

void reqOpenMatch(obs_data_t *request, obs_data_t *response, void *)
{
	const QString id = QString::fromUtf8(obs_data_get_string(request, "id"));
	if (id.isEmpty())
		return fail(response, "pass \"id\" (see GetMatches)");
	queueAction(response, [id](ReplayController &c) { c.openMatch(id); });
}

void reqSetMatchPhase(obs_data_t *request, obs_data_t *response, void *)
{
	if (!obs_data_has_user_value(request, "phase"))
		return fail(response, "pass \"phase\" (0 = pre-match; see GetMatch)");
	const int phase = (int)obs_data_get_int(request, "phase");
	queueAction(response, [phase](ReplayController &c) { c.setMatchPhase(phase); });
}

void reqPlayHighlights(obs_data_t *request, obs_data_t *response, void *)
{
	const int reel = obs_data_has_user_value(request, "reel") ? (int)obs_data_get_int(request, "reel") : -1;
	const bool hasLoop = obs_data_has_user_value(request, "loop");
	const bool loop = obs_data_get_bool(request, "loop");
	queueAction(response, [reel, hasLoop, loop](ReplayController &c) {
		if (hasLoop)
			c.setHighlightsLoop(loop);
		c.playHighlights(reel);
	});
}

void reqExportHighlights(obs_data_t *request, obs_data_t *response, void *)
{
	const int reel = obs_data_has_user_value(request, "reel") ? (int)obs_data_get_int(request, "reel") : -1;
	const int angle = obs_data_has_user_value(request, "angle") ? (int)obs_data_get_int(request, "angle") - 1 : -1;
	queueAction(response, [reel, angle](ReplayController &c) { c.exportHighlights(reel, angle); });
}

void reqSetHighlight(obs_data_t *request, obs_data_t *response, void *)
{
	std::string id;
	if (!resolveEventId(request, response, id))
		return;
	const bool hasValue = obs_data_has_user_value(request, "highlight");
	const bool on = obs_data_get_bool(request, "highlight");
	queueAction(response, [id, hasValue, on](ReplayController &c) {
		const QString target = QString::fromStdString(id.empty() ? c.selectedEventId() : id);
		if (hasValue)
			c.setHighlight(target, on);
		else
			c.toggleHighlight(target);
	});
}

void reqSetEventPeriod(obs_data_t *request, obs_data_t *response, void *)
{
	std::string id;
	if (!resolveEventId(request, response, id))
		return;
	if (id.empty() || !obs_data_has_user_value(request, "period"))
		return fail(response, "pass \"id\" (or \"index\") and \"period\"");
	const int period = (int)obs_data_get_int(request, "period");
	queueAction(response,
		    [id, period](ReplayController &c) { c.setEventPeriod(QString::fromStdString(id), period); });
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
	{"GetMatch", reqGetMatch},
	{"SetMatchPhase", reqSetMatchPhase},
	{"NextMatchPhase", reqNextMatchPhase},
	{"NewMatch", reqNewMatch},
	{"GetMatches", reqGetMatches},
	{"OpenMatch", reqOpenMatch},
	{"SetMatchInfo", reqSetMatchInfo},
	{"PlayHighlights", reqPlayHighlights},
	{"ExportHighlights", reqExportHighlights},
	{"SetHighlight", reqSetHighlight},
	{"SetEventPeriod", reqSetEventPeriod},
	{"StartCleanFeed", reqStartCleanFeed},
	{"StopCleanFeed", reqStopCleanFeed},
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
