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

#include "replay-controller.hpp"
#include "ffmpeg-compat.hpp"
#include "rc-common.hpp"
#include "websocket-api.hpp"

#include <graphics/vec2.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace rc {

/* ------------------------------------------------------------------------- */
/* Hotkeys                                                                   */

struct HotkeyBinding {
	ReplayController *owner = nullptr;
	const char *name = nullptr;
	void (*action)(ReplayController &) = nullptr;
	obs_hotkey_id id = OBS_INVALID_HOTKEY_ID;
};

namespace {

struct HotkeyDef {
	const char *name;
	const char *text;
	void (*action)(ReplayController &);
};

const HotkeyDef kHotkeys[] = {
	{"ReplayCue.ToggleArm", "Hotkey.ToggleArm",
	 [](ReplayController &c) {
		 c.toggleArm();
	 }},
	{"ReplayCue.MarkIn", "Hotkey.MarkIn",
	 [](ReplayController &c) {
		 c.markIn();
	 }},
	{"ReplayCue.MarkOut", "Hotkey.MarkOut",
	 [](ReplayController &c) {
		 c.markOut();
	 }},
	{"ReplayCue.InstantReplay", "Hotkey.InstantReplay",
	 [](ReplayController &c) {
		 c.instantReplay(false);
	 }},
	{"ReplayCue.InstantReplayPlay", "Hotkey.InstantReplayPlay",
	 [](ReplayController &c) {
		 c.instantReplay(true);
	 }},
	{"ReplayCue.CueSelected", "Hotkey.CueSelected",
	 [](ReplayController &c) {
		 c.cueSelected();
	 }},
	{"ReplayCue.PlaySelected", "Hotkey.PlaySelected",
	 [](ReplayController &c) {
		 c.playSelected();
	 }},
	{"ReplayCue.PlayLatest", "Hotkey.PlayLatest",
	 [](ReplayController &c) {
		 c.playLatest();
	 }},
	{"ReplayCue.PlayPause", "Hotkey.PlayPause",
	 [](ReplayController &c) {
		 c.togglePause();
	 }},
	{"ReplayCue.Restart", "Hotkey.Restart",
	 [](ReplayController &c) {
		 c.restart();
	 }},
	{"ReplayCue.ReturnToLive", "Hotkey.ReturnToLive",
	 [](ReplayController &c) {
		 c.returnToLive();
	 }},
	{"ReplayCue.NextAngle", "Hotkey.NextAngle",
	 [](ReplayController &c) {
		 c.nextAngle();
	 }},
	{"ReplayCue.PrevAngle", "Hotkey.PrevAngle",
	 [](ReplayController &c) {
		 c.prevAngle();
	 }},
	{"ReplayCue.Angle1", "Hotkey.Angle1",
	 [](ReplayController &c) {
		 c.setAngle(0);
	 }},
	{"ReplayCue.Angle2", "Hotkey.Angle2",
	 [](ReplayController &c) {
		 c.setAngle(1);
	 }},
	{"ReplayCue.Angle3", "Hotkey.Angle3",
	 [](ReplayController &c) {
		 c.setAngle(2);
	 }},
	{"ReplayCue.Angle4", "Hotkey.Angle4",
	 [](ReplayController &c) {
		 c.setAngle(3);
	 }},
	{"ReplayCue.Angle5", "Hotkey.Angle5",
	 [](ReplayController &c) {
		 c.setAngle(4);
	 }},
	{"ReplayCue.Angle6", "Hotkey.Angle6",
	 [](ReplayController &c) {
		 c.setAngle(5);
	 }},
	{"ReplayCue.Angle7", "Hotkey.Angle7",
	 [](ReplayController &c) {
		 c.setAngle(6);
	 }},
	{"ReplayCue.Angle8", "Hotkey.Angle8",
	 [](ReplayController &c) {
		 c.setAngle(7);
	 }},
	{"ReplayCue.Speed25", "Hotkey.Speed25",
	 [](ReplayController &c) {
		 c.setSpeed(0.25);
	 }},
	{"ReplayCue.Speed50", "Hotkey.Speed50",
	 [](ReplayController &c) {
		 c.setSpeed(0.5);
	 }},
	{"ReplayCue.Speed75", "Hotkey.Speed75",
	 [](ReplayController &c) {
		 c.setSpeed(0.75);
	 }},
	{"ReplayCue.Speed100", "Hotkey.Speed100",
	 [](ReplayController &c) {
		 c.setSpeed(1.0);
	 }},
	{"ReplayCue.StepBack", "Hotkey.StepBack",
	 [](ReplayController &c) {
		 c.step(-1);
	 }},
	{"ReplayCue.StepForward", "Hotkey.StepForward",
	 [](ReplayController &c) {
		 c.step(1);
	 }},
	{"ReplayCue.SelectPrev", "Hotkey.SelectPrev",
	 [](ReplayController &c) {
		 c.selectRelative(-1);
	 }},
	{"ReplayCue.SelectNext", "Hotkey.SelectNext",
	 [](ReplayController &c) {
		 c.selectRelative(1);
	 }},
	{"ReplayCue.PlayPrevEvent", "Hotkey.PlayPrevEvent",
	 [](ReplayController &c) {
		 c.playPreviousEvent();
	 }},
	{"ReplayCue.PlayNextEvent", "Hotkey.PlayNextEvent",
	 [](ReplayController &c) {
		 c.playNextEvent();
	 }},
	{"ReplayCue.NextMatchPhase", "Hotkey.NextMatchPhase",
	 [](ReplayController &c) {
		 c.nextMatchPhase();
	 }},
	{"ReplayCue.PlayHighlights", "Hotkey.PlayHighlights",
	 [](ReplayController &c) {
		 c.playHighlights();
	 }},
	{"ReplayCue.ToggleHighlight", "Hotkey.ToggleHighlight",
	 [](ReplayController &c) {
		 c.toggleHighlight(QString::fromStdString(c.selectedEventId()));
	 }},
};

void hotkeyPressed(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed)
		return;
	auto *binding = static_cast<HotkeyBinding *>(data);
	/* hotkeys fire on the hotkey thread; everything else lives on the UI thread */
	QMetaObject::invokeMethod(
		binding->owner, [binding]() { binding->action(*binding->owner); }, Qt::QueuedConnection);
}

QString qstr(const std::string &s)
{
	return QString::fromStdString(s);
}

OBSSourceAutoRelease findTransition(const std::string &name)
{
	OBSSourceAutoRelease found;
	if (name.empty())
		return found;
	obs_frontend_source_list list = {};
	obs_frontend_get_transitions(&list);
	for (size_t i = 0; i < list.sources.num; i++) {
		obs_source_t *tr = list.sources.array[i];
		if (name == obs_source_get_name(tr)) {
			found = obs_source_get_ref(tr);
			break;
		}
	}
	obs_frontend_source_list_free(&list);
	return found;
}

bool sameCapture(const Config &a, const Config &b)
{
	if (a.angles.size() != b.angles.size())
		return false;
	for (size_t i = 0; i < a.angles.size(); i++)
		if (a.angles[i].label != b.angles[i].label || a.angles[i].source != b.angles[i].source ||
		    a.angles[i].enabled != b.angles[i].enabled)
			return false;
	return a.encoderId == b.encoderId && a.bitrateKbps == b.bitrateKbps && a.keyintSec == b.keyintSec &&
	       a.maxHeight == b.maxHeight && a.bufferSeconds == b.bufferSeconds && a.audioTrack == b.audioTrack;
}

std::string uniquePath(const std::string &dir, const std::string &stem, const std::string &ext)
{
	std::string path = joinPath(dir, stem + ext);
	for (int n = 2; QFileInfo::exists(qstr(path)); n++)
		path = joinPath(dir, stem + "_" + std::to_string(n) + ext);
	return path;
}

} // namespace

/* ------------------------------------------------------------------------- */

ReplayController::ReplayController(QObject *parent) : QObject(parent)
{
	cfg.load();
	speedValue = cfg.defaultSpeed;

	std::string details;
	if (!ffmpegAbiCompatible(details))
		ffmpegProblem = QString(text("Msg.FfmpegMismatch")).arg(qstr(details));

	statusData = obs_data_create();
	eventsData = obs_data_create();
	matchData = obs_data_create();
	rebuildMatchSnapshot();

	writer = std::make_unique<ClipWriter>([this](ClipResult result) {
		QMetaObject::invokeMethod(
			this, [this, result]() mutable { onClipWritten(std::move(result)); }, Qt::QueuedConnection);
	});

	watcher = new QFileSystemWatcher(this);
	rescanTimer = new QTimer(this);
	rescanTimer->setSingleShot(true);
	rescanTimer->setInterval(700);
	connect(watcher, &QFileSystemWatcher::directoryChanged, this, [this]() { rescanTimer->start(); });
	connect(rescanTimer, &QTimer::timeout, this, &ReplayController::refreshEvents);

	pollTimer = new QTimer(this);
	pollTimer->setInterval(50);
	connect(pollTimer, &QTimer::timeout, this, &ReplayController::poll);
	pollTimer->start();

	tickTimer = new QTimer(this);
	tickTimer->setInterval(1000);
	connect(tickTimer, &QTimer::timeout, this, [this]() {
		engine.tick();
		emit statusChanged();
	});
	tickTimer->start();

	obs_frontend_add_event_callback(frontendEvent, this);
	obs_frontend_add_save_callback(frontendSave, this);

	registerHotkeys();
	loadHotkeys();
}

ReplayController::~ReplayController()
{
	shutdown();
}

void ReplayController::shutdown()
{
	if (shuttingDown)
		return;
	shuttingDown = true;

	saveHotkeys();
	obs_frontend_remove_event_callback(frontendEvent, this);
	obs_frontend_remove_save_callback(frontendSave, this);

	for (auto &binding : hotkeys)
		if (binding->id != OBS_INVALID_HOTKEY_ID)
			obs_hotkey_unregister(binding->id);
	hotkeys.clear();

	delete pollTimer;
	delete tickTimer;
	delete rescanTimer;
	delete watcher;
	pollTimer = tickTimer = rescanTimer = nullptr;
	watcher = nullptr;

	engine.disarm();
	writer.reset();
	if (scanThread.joinable())
		scanThread.join();
}

bool ReplayController::ffmpegOk() const
{
	return ffmpegReady();
}

void ReplayController::say(const QString &msg, bool error)
{
	obs_log(error ? LOG_WARNING : LOG_INFO, "%s", msg.toUtf8().constData());
	emit message(msg, error);
}

/* ------------------------------------------------------------------------- */
/* Frontend integration                                                      */

void ReplayController::frontendEvent(enum obs_frontend_event event, void *data)
{
	static_cast<ReplayController *>(data)->onFrontendEvent(event);
}

void ReplayController::frontendSave(obs_data_t *, bool saving, void *param)
{
	if (saving)
		static_cast<ReplayController *>(param)->saveHotkeys();
}

void ReplayController::onFrontendEvent(enum obs_frontend_event event)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		initialize();
		break;

	case OBS_FRONTEND_EVENT_TRANSITION_STOPPED:
		if (transitionOverridden)
			restoreTransition();
		if (stopAfterTransition) {
			stopAfterTransition = false;
			PlayerRef ref = acquirePlayer(false);
			if (ref.player)
				ref.player->stop(true);
		}
		break;

	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
		if (cued && cfg.startCuedOnLive && replaySceneIsLive()) {
			resume();
		} else if (!playingId.empty() && !stopAfterTransition && !pendingLiveScene && !cued &&
			   !replaySceneIsLive()) {
			/* the operator cut away from the replay by hand */
			PlayerRef ref = acquirePlayer(false);
			if (ref.player)
				ref.player->pause();
			playingId.clear();
			queue.clear();
			highlightSession = false;
			emit playbackUpdated();
		}
		break;

	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_SAVED:
		importReplayBufferSave();
		break;

	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING:
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP:
		/* our views hold references to the old collection's sources */
		if (engine.armed()) {
			wasArmedBeforeCollectionChange = true;
			disarm();
		}
		playingId.clear();
		queue.clear();
		highlightSession = false;
		playerWeak = nullptr;
		previousScene = nullptr;
		pendingLiveScene = nullptr;
		break;

	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		if (wasArmedBeforeCollectionChange) {
			wasArmedBeforeCollectionChange = false;
			arm();
		}
		break;

	case OBS_FRONTEND_EVENT_EXIT:
		shutdown();
		break;

	default:
		break;
	}
}

void ReplayController::initialize()
{
	if (initialized)
		return;
	initialized = true;

	if (!ffmpegProblem.isEmpty())
		say(ffmpegProblem, true);

	QDir().mkpath(qstr(cfg.folder));
	refreshEvents();
	if (cfg.armOnStartup)
		arm();
}

void ReplayController::registerHotkeys()
{
	for (const HotkeyDef &def : kHotkeys) {
		auto binding = std::make_unique<HotkeyBinding>();
		binding->owner = this;
		binding->name = def.name;
		binding->action = def.action;
		binding->id = obs_hotkey_register_frontend(def.name, text(def.text), hotkeyPressed, binding.get());
		hotkeys.push_back(std::move(binding));
	}
}

void ReplayController::loadHotkeys()
{
	obs_data_t *data = obs_data_create_from_json_file_safe(Config::configPath().c_str(), "bak");
	if (!data)
		return;
	obs_data_t *saved = obs_data_get_obj(data, "hotkeys");
	if (saved) {
		for (auto &binding : hotkeys) {
			obs_data_array_t *keys = obs_data_get_array(saved, binding->name);
			if (keys) {
				obs_hotkey_load(binding->id, keys);
				obs_data_array_release(keys);
			}
		}
		obs_data_release(saved);
	}
	obs_data_release(data);
}

void ReplayController::saveHotkeys()
{
	if (hotkeys.empty())
		return;
	const std::string path = Config::configPath();
	if (path.empty())
		return;

	obs_data_t *data = obs_data_create_from_json_file_safe(path.c_str(), "bak");
	if (!data)
		data = obs_data_create();
	obs_data_t *saved = obs_data_create();
	for (auto &binding : hotkeys) {
		obs_data_array_t *keys = obs_hotkey_save(binding->id);
		obs_data_set_array(saved, binding->name, keys);
		obs_data_array_release(keys);
	}
	obs_data_set_obj(data, "hotkeys", saved);
	obs_data_release(saved);

	char *dir = obs_module_config_path("");
	if (dir) {
		os_mkdirs(dir);
		bfree(dir);
	}
	obs_data_save_json_pretty_safe(data, path.c_str(), "tmp", "bak");
	obs_data_release(data);
}

/* ------------------------------------------------------------------------- */
/* Configuration                                                             */

void ReplayController::applyConfig(const Config &updated)
{
	const bool captureChanged = !sameCapture(cfg, updated);
	const bool folderChanged = cfg.folder != updated.folder || cfg.angleLabels() != updated.angleLabels() ||
				   cfg.watchFolder != updated.watchFolder;
	const bool sportChanged = cfg.sport != updated.sport;

	/* the match moves on while the settings dialog is open; a new sport
	 * starts a new match */
	const int phase = cfg.matchPhase;
	const std::string match = cfg.matchId;
	cfg = updated;
	cfg.matchPhase = sportChanged ? 0 : phase;
	cfg.matchId = sportChanged ? std::string() : match;
	cfg.save();

	if (captureChanged && engine.armed())
		arm();
	if (folderChanged) {
		QDir().mkpath(qstr(cfg.folder));
		refreshEvents();
	}
	updateWatcher();
	rebuildMatchSnapshot();
	emit statusChanged();
	if (sportChanged)
		emit matchChanged();
}

/* ------------------------------------------------------------------------- */
/* Capture and marking                                                       */

void ReplayController::toggleArm()
{
	if (engine.armed())
		disarm();
	else
		arm();
}

void ReplayController::arm()
{
	std::vector<AngleSpec> angles = cfg.enabledAngles();
	if (angles.empty()) {
		say(text("Msg.NoAngles"), true);
		return;
	}
	engine.arm(angles, cfg.captureSettings());
	say(QString(text("Msg.Armed")).arg(angles.size()));
	emit statusChanged();

	OBSDataAutoRelease data = obs_data_create();
	obs_data_set_bool(data, "armed", true);
	emitVendor("ArmedChanged", data);
}

void ReplayController::disarm()
{
	if (!engine.armed())
		return;
	engine.disarm();
	markInTime = 0;
	say(text("Msg.Disarmed"));
	emit statusChanged();

	OBSDataAutoRelease data = obs_data_create();
	obs_data_set_bool(data, "armed", false);
	emitVendor("ArmedChanged", data);
}

void ReplayController::markIn()
{
	if (!engine.armed()) {
		say(text("Msg.NotArmed"), true);
		return;
	}
	markInTime = nowUsec() - (int64_t)cfg.markLatencyMs * 1000;
	emit statusChanged();
}

void ReplayController::markOut()
{
	if (!engine.armed()) {
		say(text("Msg.NotArmed"), true);
		return;
	}
	const int64_t out = nowUsec() - (int64_t)cfg.markLatencyMs * 1000;
	int64_t in = out - (int64_t)cfg.instantSeconds * 1000000;
	if (markInTime && markInTime < out - 100000)
		in = markInTime;
	markInTime = 0;
	createEvent(in, out, false);
}

void ReplayController::clearMarks()
{
	markInTime = 0;
	emit statusChanged();
}

void ReplayController::instantReplay(bool play, int seconds)
{
	if (!engine.armed()) {
		say(text("Msg.NotArmed"), true);
		return;
	}
	const int length = seconds > 0 ? seconds : cfg.instantSeconds;
	const int64_t out = nowUsec() - (int64_t)cfg.markLatencyMs * 1000;
	createEvent(out - (int64_t)length * 1000000, out, play);
}

void ReplayController::createEvent(int64_t inUsec, int64_t outUsec, bool play)
{
	if (!ffmpegOk()) {
		say(ffmpegProblem, true);
		return;
	}
	auto buffers = engine.videoBuffers();
	if (buffers.empty()) {
		say(text("Msg.NothingRecorded"), true);
		return;
	}

	const int64_t oldest = engine.oldestCommonUsec();
	if (inUsec < oldest)
		inUsec = oldest;

	ClipJob job;
	job.number = nextNumber++;
	job.name = std::string(text("Event.DefaultName")) + " " + std::to_string(job.number);
	job.highlight = cfg.highlightNewReplays;
	job.period = sport().periods ? periodOfPhase(cfg.matchPhase) : 0;
	job.match = cfg.matchId;
	job.folder = cfg.folder;
	job.created = wallClockSeconds() - (nowUsec() - outUsec) / 1000000;
	char suffix[16];
	snprintf(suffix, sizeof(suffix), "_R%03d", job.number);
	job.baseName = formatFileStamp(job.created) + suffix;
	job.inUsec = inUsec;
	job.outUsec = outUsec;
	job.tailUsec = (int64_t)cfg.tailMs * 1000;
	job.angles = std::move(buffers);
	job.audio = engine.audioBuffer();
	job.playWhenReady = play;

	pendingClips++;
	writer->enqueue(std::move(job));
	emit statusChanged();
}

void ReplayController::onClipWritten(ClipResult result)
{
	pendingClips = std::max(0, pendingClips - 1);
	emit statusChanged();

	if (!result.ok) {
		say(QString(text("Msg.ReplayFailed")).arg(qstr(result.error)), true);
		return;
	}

	ReplayEvent &added = result.event;
	auto it = std::find_if(eventList.begin(), eventList.end(),
			       [&](const ReplayEvent &e) { return e.id == added.id; });
	if (it != eventList.end())
		*it = added;
	else
		eventList.push_back(added);
	recentWrites.insert(added.id);
	std::stable_sort(eventList.begin(), eventList.end(),
			 [](const ReplayEvent &a, const ReplayEvent &b) { return a.created < b.created; });

	selectedId = added.id;
	rebuildEventsSnapshot();
	emit eventsChanged();
	emit selectionChanged(qstr(selectedId));
	say(QString(text("Msg.ReplaySaved"))
		    .arg(qstr(added.name))
		    .arg(added.angles.size())
		    .arg(added.duration, 0, 'f', 1));

	OBSDataAutoRelease data = eventToData(added);
	emitVendor("ReplayCreated", data);

	if (result.playWhenReady)
		playEvent(qstr(added.id));
}

/* ------------------------------------------------------------------------- */
/* Event list                                                                */

const ReplayEvent *ReplayController::findEvent(const std::string &id) const
{
	for (const ReplayEvent &e : eventList)
		if (e.id == id)
			return &e;
	return nullptr;
}

ReplayEvent *ReplayController::findEventMutable(const std::string &id)
{
	for (ReplayEvent &e : eventList)
		if (e.id == id)
			return &e;
	return nullptr;
}

int ReplayController::indexOfEvent(const std::string &id) const
{
	for (size_t i = 0; i < eventList.size(); i++)
		if (eventList[i].id == id)
			return (int)i;
	return -1;
}

std::vector<std::string> ReplayController::activeAngleLabels() const
{
	if (!lastStatus.angleLabels.empty() && lastStatus.state != PlayerState::Idle)
		return lastStatus.angleLabels;
	if (const ReplayEvent *e = findEvent(selectedId))
		return e->angleLabels();
	std::vector<std::string> labels;
	for (const AngleSpec &a : cfg.enabledAngles())
		labels.push_back(a.label);
	return labels;
}

void ReplayController::refreshEvents()
{
	if (shuttingDown)
		return;
	if (scanning) {
		rescanQueued = true;
		return;
	}
	scanning = true;
	if (scanThread.joinable())
		scanThread.join();

	const std::string folder = cfg.folder;
	const std::vector<std::string> labels = cfg.angleLabels();
	scanThread = std::thread([this, folder, labels]() {
		std::vector<ReplayEvent> scanned = scanReplayFolder(folder, labels, probeCache);
		QMetaObject::invokeMethod(
			this, [this, scanned]() mutable { onScanFinished(std::move(scanned)); }, Qt::QueuedConnection);
	});
}

void ReplayController::onScanFinished(std::vector<ReplayEvent> scanned)
{
	scanning = false;
	if (scanThread.joinable())
		scanThread.join();

	/* a clip finished writing while the scan ran: keep it */
	for (auto it = recentWrites.begin(); it != recentWrites.end();) {
		bool present =
			std::any_of(scanned.begin(), scanned.end(), [&](const ReplayEvent &e) { return e.id == *it; });
		if (present) {
			it = recentWrites.erase(it);
		} else {
			if (const ReplayEvent *e = findEvent(*it))
				scanned.push_back(*e);
			++it;
		}
	}
	std::stable_sort(scanned.begin(), scanned.end(),
			 [](const ReplayEvent &a, const ReplayEvent &b) { return a.created < b.created; });

	eventList = std::move(scanned);
	int highest = 0;
	for (const ReplayEvent &e : eventList)
		highest = std::max(highest, e.number);
	nextNumber = std::max(nextNumber, highest + 1);

	if (!findEvent(selectedId))
		selectedId = eventList.empty() ? std::string() : eventList.back().id;

	rebuildEventsSnapshot();
	emit eventsChanged();
	emit selectionChanged(qstr(selectedId));
	updateWatcher();

	if (rescanQueued) {
		rescanQueued = false;
		refreshEvents();
	}
}

void ReplayController::updateWatcher()
{
	if (!watcher)
		return;
	const QStringList watched = watcher->directories();
	if (!watched.isEmpty())
		watcher->removePaths(watched);
	if (!cfg.watchFolder)
		return;

	QDir root(qstr(cfg.folder));
	if (!root.exists())
		return;
	QStringList paths{root.absolutePath()};
	for (const QString &sub : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
		paths << root.absoluteFilePath(sub);
	watcher->addPaths(paths);
}

void ReplayController::selectEvent(const QString &id)
{
	const std::string sid = id.toStdString();
	if (sid == selectedId)
		return;
	selectedId = sid;
	emit selectionChanged(id);
	emit playbackUpdated();
}

void ReplayController::selectRelative(int delta)
{
	if (eventList.empty())
		return;
	int idx = indexOfEvent(selectedId);
	if (idx < 0)
		idx = delta > 0 ? -1 : (int)eventList.size();
	idx = std::clamp(idx + delta, 0, (int)eventList.size() - 1);
	selectEvent(qstr(eventList[(size_t)idx].id));
}

bool ReplayController::persist(ReplayEvent &event)
{
	if (event.sidecar.empty()) {
		/* imported clips get a sidecar the first time they are edited */
		QDir().mkpath(qstr(cfg.folder));
		event.sidecar = uniquePath(cfg.folder, "import_" + sanitizeFileName(event.name), kSidecarExtension);
	}
	std::string error;
	if (!saveSidecar(event, error)) {
		say(qstr(error), true);
		return false;
	}
	recentWrites.insert(event.id);
	return true;
}

void ReplayController::renameEvent(const QString &id, const QString &name)
{
	ReplayEvent *event = findEventMutable(id.toStdString());
	if (!event || name.trimmed().isEmpty())
		return;
	event->name = name.trimmed().toStdString();
	persist(*event);
	rebuildEventsSnapshot();
	emit eventsChanged();
}

void ReplayController::setEventTag(const QString &id, const QString &tag)
{
	ReplayEvent *event = findEventMutable(id.toStdString());
	if (!event)
		return;
	event->tag = tag.trimmed().toStdString();
	persist(*event);
	rebuildEventsSnapshot();
	emit eventsChanged();
}

void ReplayController::toggleFavorite(const QString &id)
{
	ReplayEvent *event = findEventMutable(id.toStdString());
	if (!event)
		return;
	event->favorite = !event->favorite;
	persist(*event);
	rebuildEventsSnapshot();
	emit eventsChanged();
}

/* ------------------------------------------------------------------------- */
/* Match and highlights                                                      */

bool ReplayController::inReel(const ReplayEvent &event, int reel) const
{
	const Sport &s = sport();
	if (s.periods == 0)
		return true;
	return event.match == cfg.matchId && reelIncludes(s, reel, event.period);
}

QStringList ReplayController::reelIds(int reel) const
{
	QStringList ids;
	for (const ReplayEvent &e : eventList)
		if (e.highlight && inReel(e, reel))
			ids << qstr(e.id);
	return ids;
}

void ReplayController::matchUpdated()
{
	const Sport &s = sport();
	cfg.save();
	rebuildMatchSnapshot();
	emit matchChanged();

	OBSDataAutoRelease data = obs_data_create();
	obs_data_set_int(data, "phase", cfg.matchPhase);
	obs_data_set_string(data, "name", phaseName(s, cfg.matchPhase).c_str());
	obs_data_set_int(data, "period", periodOfPhase(cfg.matchPhase));
	obs_data_set_bool(data, "break", isBreakPhase(s, cfg.matchPhase));
	obs_data_set_int(data, "reel", currentReel());
	obs_data_set_string(data, "match_id", cfg.matchId.c_str());
	emitVendor("MatchPhaseChanged", data);
}

void ReplayController::setMatchPhase(int phase)
{
	const Sport &s = sport();
	phase = std::clamp(phase, 0, phaseCount(s) - 1);
	if (phase == cfg.matchPhase)
		return;
	cfg.matchPhase = phase;
	if (phase > 0 && cfg.matchId.empty())
		cfg.matchId = formatFileStamp(wallClockSeconds());

	const QString name = qstr(phaseName(s, phase));
	if (isBreakPhase(s, phase))
		say(QString(text("Msg.BreakStarted")).arg(name));
	else if (phase > 0)
		say(QString(text("Msg.PeriodStarted")).arg(name));
	matchUpdated();
}

void ReplayController::nextMatchPhase()
{
	const Sport &s = sport();
	if (s.periods == 0) {
		say(text("Msg.NoSport"), true);
		return;
	}
	if (isFullTime(s, cfg.matchPhase)) {
		say(text("Msg.MatchOver"), true);
		return;
	}
	setMatchPhase(cfg.matchPhase + 1);
}

void ReplayController::newMatch()
{
	cfg.matchPhase = 0;
	cfg.matchId.clear();
	say(text("Msg.NewMatch"));
	matchUpdated();
}

void ReplayController::playHighlights(int reel)
{
	const std::vector<int> reels = reelsOf(sport());
	if (std::find(reels.begin(), reels.end(), reel) == reels.end())
		reel = currentReel();

	const QStringList ids = reelIds(reel);
	const QString name = qstr(reelName(sport(), reel));
	if (ids.isEmpty()) {
		say(QString(text("Msg.NoHighlights")).arg(name), true);
		return;
	}

	highlightSession = true;
	highlightReel = reel;
	playReelFrom(ids, 0);

	double duration = 0;
	for (const QString &id : ids)
		if (const ReplayEvent *e = findEvent(id.toStdString()))
			duration += e->duration;
	say(QString(text("Msg.HighlightsStarted")).arg(name).arg(ids.size()));

	OBSDataAutoRelease data = obs_data_create();
	obs_data_set_int(data, "reel", reel);
	obs_data_set_string(data, "name", name.toUtf8().constData());
	obs_data_set_int(data, "count", ids.size());
	obs_data_set_double(data, "duration", duration);
	obs_data_set_bool(data, "loop", cfg.loopHighlights);
	emitVendor("HighlightsStarted", data);
}

void ReplayController::playReelFrom(const QStringList &ids, int index)
{
	if (index < 0 || index >= ids.size())
		return;
	const ReplayEvent *event = findEvent(ids[index].toStdString());
	if (!event)
		return;
	queue = ids.mid(index + 1);
	startEvent(*event, angleIndex, true, true);
}

void ReplayController::setHighlightsLoop(bool enabled)
{
	cfg.loopHighlights = enabled;
	cfg.save();
	rebuildMatchSnapshot();
	emit statusChanged();
}

void ReplayController::adoptIntoMatch(ReplayEvent &event)
{
	/* an imported clip, or one saved before kick-off, joins the current match */
	if (event.match.empty())
		event.match = cfg.matchId;
	if (event.period == 0 && sport().periods)
		event.period = periodOfPhase(cfg.matchPhase);
}

void ReplayController::setHighlight(const QString &id, bool on)
{
	ReplayEvent *event = findEventMutable(id.toStdString());
	if (!event || event->highlight == on)
		return;
	event->highlight = on;
	if (on)
		adoptIntoMatch(*event);
	persist(*event);
	rebuildEventsSnapshot();
	emit eventsChanged();
}

void ReplayController::toggleHighlight(const QString &id)
{
	const ReplayEvent *event = findEvent(id.toStdString());
	if (!event) {
		say(text("Msg.NoReplay"), true);
		return;
	}
	setHighlight(id, !event->highlight);
}

void ReplayController::setEventPeriod(const QString &id, int period)
{
	ReplayEvent *event = findEventMutable(id.toStdString());
	if (!event)
		return;
	event->period = std::clamp(period, 0, sport().periods);
	if (event->period > 0)
		adoptIntoMatch(*event);
	persist(*event);
	rebuildEventsSnapshot();
	emit eventsChanged();
}

void ReplayController::deleteEvents(const QStringList &ids)
{
	int removed = 0;
	for (const QString &qid : ids) {
		const std::string id = qid.toStdString();
		const ReplayEvent *event = findEvent(id);
		if (!event)
			continue;
		if (id == playingId)
			returnToLive();

		std::vector<std::string> files;
		for (const AngleClip &clip : event->angles)
			files.push_back(clip.file);
		if (!event->sidecar.empty())
			files.push_back(event->sidecar);
		for (const std::string &file : files) {
			QString path = qstr(file);
			if (QFileInfo::exists(path) && !QFile::moveToTrash(path))
				QFile::remove(path);
		}

		eventList.erase(eventList.begin() + indexOfEvent(id));
		recentWrites.erase(id);
		removed++;
	}
	if (!removed)
		return;
	if (!findEvent(selectedId))
		selectedId = eventList.empty() ? std::string() : eventList.back().id;
	rebuildEventsSnapshot();
	emit eventsChanged();
	emit selectionChanged(qstr(selectedId));
	say(QString(text("Msg.Deleted")).arg(removed));
}

void ReplayController::exportEvent(const QString &id, int angle, bool trimToMarks)
{
	const ReplayEvent *event = findEvent(id.toStdString());
	if (!event)
		return;

	std::vector<ExportItem> items;
	for (size_t i = 0; i < event->angles.size(); i++) {
		if (angle >= 0 && (size_t)angle != i)
			continue;
		ExportItem item;
		item.input = event->angles[i].file;
		const std::string stem = sanitizeFileName(event->name) + "_" + sanitizeFileName(event->angles[i].label);
		item.output = uniquePath(cfg.exportFolder, stem, ".mp4");
		if (trimToMarks && event->inUsec) {
			item.start = event->inOffset(i);
			item.end = item.start + event->duration;
		}
		items.push_back(item);
	}
	if (items.empty())
		return;

	say(QString(text("Msg.Exporting")).arg(items.size()));
	writer->enqueueExport(std::move(items), [this](bool ok, std::string msg) {
		QMetaObject::invokeMethod(this, [this, ok, msg]() { say(qstr(msg), !ok); }, Qt::QueuedConnection);
	});
}

void ReplayController::importReplayBufferSave()
{
	if (!cfg.importReplayBuffer)
		return;
	char *raw = obs_frontend_get_last_replay();
	std::string path = raw ? raw : "";
	bfree(raw);
	if (path.empty())
		return;

	if (QFileInfo(qstr(dirNameOf(path))).canonicalFilePath() == QDir(qstr(cfg.folder)).canonicalPath()) {
		refreshEvents(); /* it landed in the replay folder already */
		return;
	}

	ReplayEvent event;
	event.id = "obs:" + stemOf(path);
	event.name = stemOf(path);
	event.imported = true;
	event.created = wallClockSeconds();
	AngleClip clip;
	clip.label = text("Event.Program");
	clip.file = path;
	clip.duration = probeDuration(path);
	event.duration = clip.duration;
	event.angles.push_back(clip);
	event.sidecar = uniquePath(cfg.folder, "obs_" + sanitizeFileName(event.name), kSidecarExtension);
	if (!persist(event))
		return;

	eventList.push_back(event);
	selectedId = event.id;
	rebuildEventsSnapshot();
	emit eventsChanged();
	emit selectionChanged(qstr(selectedId));
	say(QString(text("Msg.ImportedReplayBuffer")).arg(qstr(event.name)));
}

/* ------------------------------------------------------------------------- */
/* Player and scenes                                                         */

bool ReplayController::ensureScene(const std::string &name, OBSSourceAutoRelease &scene, obs_source_t *player,
				   bool create)
{
	scene = obs_get_source_by_name(name.c_str());
	if (scene && !obs_scene_from_source(scene)) {
		say(QString(text("Msg.NameConflict")).arg(qstr(name)), true);
		scene = nullptr;
		return false;
	}
	if (!scene) {
		if (!create)
			return false;
		obs_scene_t *created = obs_scene_create(name.c_str());
		scene = obs_source_get_ref(obs_scene_get_source(created));
		obs_scene_release(created);
		say(QString(text("Msg.SceneCreated")).arg(qstr(name)));
	}

	if (player && create) {
		obs_scene_t *sc = obs_scene_from_source(scene);
		struct Find {
			obs_source_t *target;
			bool found;
		} find = {player, false};
		obs_scene_enum_items(
			sc,
			[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
				auto *f = static_cast<Find *>(param);
				if (obs_sceneitem_get_source(item) == f->target)
					f->found = true;
				return !f->found;
			},
			&find);

		if (!find.found) {
			obs_sceneitem_t *item = obs_scene_add(sc, player);
			struct obs_video_info ovi = {};
			if (item && obs_get_video_info(&ovi)) {
				struct vec2 bounds;
				vec2_set(&bounds, (float)ovi.base_width, (float)ovi.base_height);
				struct vec2 pos;
				vec2_zero(&pos);
				obs_sceneitem_set_pos(item, &pos);
				obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_SCALE_INNER);
				obs_sceneitem_set_bounds_alignment(item, OBS_ALIGN_CENTER);
				obs_sceneitem_set_bounds(item, &bounds);
				obs_sceneitem_set_order(item, OBS_ORDER_MOVE_BOTTOM);
			}
		}
	}
	return true;
}

ReplayController::PlayerRef ReplayController::acquirePlayer(bool create)
{
	PlayerRef ref;
	ref.source = obs_weak_source_get_source(playerWeak);
	if (ref.source && (obs_source_removed(ref.source) || !playerFromSource(ref.source)))
		ref.source = nullptr;

	if (!ref.source) {
		ref.source = obs_get_source_by_name(cfg.playerName.c_str());
		if (ref.source && !playerFromSource(ref.source)) {
			if (create)
				say(QString(text("Msg.NameConflict")).arg(qstr(cfg.playerName)), true);
			ref.source = nullptr;
			return ref;
		}
		if (!ref.source) {
			/* renamed by the user? look inside the replay scene */
			OBSSourceAutoRelease scene = obs_get_source_by_name(cfg.sceneName.c_str());
			if (obs_scene_t *sc = obs_scene_from_source(scene)) {
				obs_scene_enum_items(
					sc,
					[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
						auto *out = static_cast<OBSSourceAutoRelease *>(param);
						obs_source_t *src = obs_sceneitem_get_source(item);
						if (playerFromSource(src)) {
							*out = obs_source_get_ref(src);
							return false;
						}
						return true;
					},
					&ref.source);
			}
		}
		if (!ref.source && create)
			ref.source = obs_source_create(kPlayerSourceId, cfg.playerName.c_str(), nullptr, nullptr);
		if (!ref.source)
			return ref;
		playerWeak = obs_source_get_weak_source(ref.source);
	}

	ref.player = playerFromSource(ref.source);
	if (create) {
		OBSSourceAutoRelease scene;
		ensureScene(cfg.sceneName, scene, ref.source, true);
	}
	return ref;
}

std::string ReplayController::sessionSceneName() const
{
	return highlightSession && !cfg.highlightsScene.empty() ? cfg.highlightsScene : cfg.sceneName;
}

bool ReplayController::isReplayScene(obs_source_t *scene) const
{
	if (!scene)
		return false;
	const char *name = obs_source_get_name(scene);
	return cfg.sceneName == name || (!cfg.highlightsScene.empty() && cfg.highlightsScene == name);
}

bool ReplayController::replaySceneIsLive()
{
	OBSSourceAutoRelease current = obs_frontend_get_current_scene();
	return current && sessionSceneName() == obs_source_get_name(current);
}

PlayRequest ReplayController::buildRequest(const ReplayEvent &event, int angle, bool autoplay) const
{
	PlayRequest request;
	request.eventId = event.id;
	request.eventName = event.name;
	for (size_t i = 0; i < event.angles.size(); i++)
		request.angles.push_back({event.angles[i].label, event.angles[i].file, event.inOffset(i)});
	request.duration = event.inUsec ? event.duration : 0.0;
	request.angle = std::clamp(angle, 0, std::max(0, (int)event.angles.size() - 1));
	request.speed = highlightSession ? cfg.highlightsSpeed : speedValue;
	request.autoplay = autoplay;
	request.audio = cfg.playAudio;
	request.hwDecode = cfg.hwDecode;
	return request;
}

void ReplayController::startEvent(const ReplayEvent &event, int angle, bool autoplay, bool goLive)
{
	if (!ffmpegOk()) {
		say(ffmpegProblem, true);
		return;
	}
	PlayerRef ref = acquirePlayer(true);
	if (!ref.player)
		return;
	const std::string sceneName = sessionSceneName();
	if (sceneName != cfg.sceneName) {
		OBSSourceAutoRelease highlightsScene;
		ensureScene(sceneName, highlightsScene, ref.source, true);
	}

	PlayRequest request = buildRequest(event, angle, autoplay);
	ref.player->load(request);
	/* a Back to Live still fading out must not stop this replay when it ends */
	stopAfterTransition = false;

	playingId = event.id;
	angleIndex = request.angle;
	cycleShown = 1;
	cycleStart = request.angle;
	cued = !autoplay;

	if (selectedId != event.id) {
		selectedId = event.id;
		emit selectionChanged(qstr(selectedId));
	}

	OBSSourceAutoRelease scene = obs_get_source_by_name(sceneName.c_str());
	if (goLive && scene && !replaySceneIsLive()) {
		/* switch once the first frame is up so the transition never
		 * reveals a stale or empty player */
		pendingLiveScene = obs_source_get_weak_source(scene);
		pendingLiveDeadline = nowUsec() + 400000;
	} else if (!goLive && scene && obs_frontend_preview_program_mode_active()) {
		obs_frontend_set_current_preview_scene(scene);
	}

	emit playbackUpdated();
	OBSDataAutoRelease data = eventToData(event);
	obs_data_set_int(data, "angle", request.angle + 1);
	obs_data_set_bool(data, "cued", cued);
	obs_data_set_bool(data, "highlights", highlightSession);
	emitVendor("PlaybackStarted", data);
}

void ReplayController::goLiveNow(obs_source_t *scene)
{
	OBSSourceAutoRelease current = obs_frontend_get_current_scene();
	if (current.Get() == scene)
		return;
	/* going from the replay scene to the highlights scene (or back) keeps
	 * the live scene to return to */
	if (!isReplayScene(current))
		previousScene = obs_source_get_weak_source(current);
	switchProgram(scene, cfg.replayTransition);
}

void ReplayController::switchProgram(obs_source_t *scene, const std::string &transitionName)
{
	OBSSourceAutoRelease transition = findTransition(transitionName);
	if (transition) {
		OBSSourceAutoRelease active = obs_frontend_get_current_transition();
		if (active.Get() != transition.Get()) {
			if (!transitionOverridden)
				savedTransition = obs_source_get_weak_source(active);
			obs_frontend_set_current_transition(transition);
			transitionOverridden = true;
			QTimer::singleShot(15000, this, [this]() { restoreTransition(); });
		}
	}
	obs_frontend_set_current_scene(scene);
}

void ReplayController::restoreTransition()
{
	if (!transitionOverridden)
		return;
	OBSSourceAutoRelease output = obs_get_output_source(0);
	if (output) {
		float t = obs_transition_get_time(output);
		if (t > 0.0f && t < 1.0f) {
			QTimer::singleShot(250, this, [this]() { restoreTransition(); });
			return;
		}
	}
	OBSSourceAutoRelease saved = obs_weak_source_get_source(savedTransition);
	if (saved)
		obs_frontend_set_current_transition(saved);
	savedTransition = nullptr;
	transitionOverridden = false;
}

/* ------------------------------------------------------------------------- */
/* Playback commands                                                         */

void ReplayController::cueEvent(const QString &id)
{
	const ReplayEvent *event = findEvent(id.toStdString());
	if (!event)
		return;
	queue.clear();
	highlightSession = false;
	startEvent(*event, angleIndex, false, false);
}

void ReplayController::cueSelected()
{
	if (!selectedId.empty())
		cueEvent(qstr(selectedId));
	else if (!eventList.empty())
		cueEvent(qstr(eventList.back().id));
}

void ReplayController::playEvent(const QString &id, int angle)
{
	const ReplayEvent *event = findEvent(id.toStdString());
	if (!event) {
		say(text("Msg.NoReplay"), true);
		return;
	}
	queue.clear();
	highlightSession = false;
	startEvent(*event, angle >= 0 ? angle : angleIndex, true, true);
}

void ReplayController::playSelected()
{
	if (cued && !playingId.empty() && playingId == selectedId) {
		resume();
		return;
	}
	if (!selectedId.empty())
		playEvent(qstr(selectedId));
	else
		playLatest();
}

void ReplayController::playLatest()
{
	if (eventList.empty()) {
		say(text("Msg.NoReplay"), true);
		return;
	}
	playEvent(qstr(eventList.back().id));
}

void ReplayController::playNextEvent()
{
	if (highlightsPlaying()) {
		/* step through the reel rather than the replay list */
		const QStringList ids = reelIds(highlightReel);
		const int idx = (int)ids.indexOf(qstr(playingId));
		if (idx + 1 < ids.size())
			playReelFrom(ids, idx + 1);
		return;
	}
	int idx = indexOfEvent(playingId.empty() ? selectedId : playingId);
	if (idx + 1 < (int)eventList.size())
		playEvent(qstr(eventList[(size_t)idx + 1].id));
}

void ReplayController::playPreviousEvent()
{
	if (highlightsPlaying()) {
		const QStringList ids = reelIds(highlightReel);
		const int idx = (int)ids.indexOf(qstr(playingId));
		playReelFrom(ids, std::max(0, idx - 1));
		return;
	}
	int idx = indexOfEvent(playingId.empty() ? selectedId : playingId);
	if (idx > 0)
		playEvent(qstr(eventList[(size_t)idx - 1].id));
	else if (idx < 0 && !eventList.empty())
		playLatest();
}

void ReplayController::playQueue(const QStringList &ids)
{
	if (ids.isEmpty())
		return;
	const ReplayEvent *first = findEvent(ids.front().toStdString());
	if (!first)
		return;
	queue = ids.mid(1);
	highlightSession = false;
	startEvent(*first, angleIndex, true, true);
}

void ReplayController::togglePause()
{
	if (lastStatus.state == PlayerState::Playing) {
		pause();
	} else if (lastStatus.state == PlayerState::Idle && playingId.empty()) {
		playSelected();
	} else {
		resume();
	}
}

void ReplayController::pause()
{
	PlayerRef ref = acquirePlayer(false);
	if (ref.player)
		ref.player->pause();
}

void ReplayController::resume()
{
	PlayerRef ref = acquirePlayer(false);
	if (!ref.player || playingId.empty())
		return;
	ref.player->play();
	cued = false;
	OBSSourceAutoRelease scene = obs_get_source_by_name(sessionSceneName().c_str());
	if (scene && !replaySceneIsLive()) {
		pendingLiveScene = obs_source_get_weak_source(scene);
		pendingLiveDeadline = nowUsec() + 400000;
	}
}

void ReplayController::restart()
{
	PlayerRef ref = acquirePlayer(false);
	if (ref.player && !playingId.empty())
		ref.player->restart();
}

void ReplayController::returnToLive()
{
	queue.clear();
	cued = false;
	pendingLiveScene = nullptr;

	PlayerRef ref = acquirePlayer(false);
	OBSSourceAutoRelease scene = obs_get_source_by_name(sessionSceneName().c_str());

	if (scene && replaySceneIsLive()) {
		OBSSourceAutoRelease target = !cfg.liveScene.empty() ? obs_get_source_by_name(cfg.liveScene.c_str())
								     : obs_weak_source_get_source(previousScene);
		if (target && target.Get() != scene.Get()) {
			if (ref.player)
				ref.player->pause();
			stopAfterTransition = true;
			switchProgram(target, cfg.returnTransition);
			QTimer::singleShot(10000, this, [this]() {
				if (!stopAfterTransition)
					return;
				stopAfterTransition = false;
				PlayerRef late = acquirePlayer(false);
				if (late.player)
					late.player->stop(true);
			});
		} else {
			say(text("Msg.NoLiveScene"), true);
			if (ref.player)
				ref.player->stop();
		}
	} else if (ref.player) {
		ref.player->stop(true);
	}

	playingId.clear();
	highlightSession = false;
	emit playbackUpdated();
	OBSDataAutoRelease data = obs_data_create();
	emitVendor("PlaybackStopped", data);
}

void ReplayController::setAngle(int angle)
{
	const auto labels = activeAngleLabels();
	if (angle < 0 || (!labels.empty() && angle >= (int)labels.size()))
		return;
	angleIndex = angle;
	PlayerRef ref = acquirePlayer(false);
	if (ref.player && !playingId.empty())
		ref.player->setAngle(angle);
	emit playbackUpdated();
}

void ReplayController::nextAngle()
{
	const int count = (int)activeAngleLabels().size();
	if (count > 0)
		setAngle((angleIndex + 1) % count);
}

void ReplayController::prevAngle()
{
	const int count = (int)activeAngleLabels().size();
	if (count > 0)
		setAngle((angleIndex + count - 1) % count);
}

void ReplayController::setSpeed(double speed)
{
	speedValue = std::clamp(speed, 0.05, 2.0);
	PlayerRef ref = acquirePlayer(false);
	if (ref.player)
		ref.player->setSpeed(speedValue);
	emit playbackUpdated();
}

void ReplayController::seek(double position)
{
	PlayerRef ref = acquirePlayer(false);
	if (ref.player)
		ref.player->seek(position);
}

void ReplayController::step(int frames)
{
	PlayerRef ref = acquirePlayer(false);
	if (ref.player)
		ref.player->step(frames);
}

void ReplayController::setAutoPlayNext(bool enabled)
{
	cfg.autoPlayNext = enabled;
	cfg.save();
	emit statusChanged();
}

void ReplayController::setCycleAngles(bool enabled)
{
	cfg.cycleAngles = enabled;
	cfg.save();
	emit statusChanged();
}

void ReplayController::setReturnToLive(bool enabled)
{
	cfg.returnToLive = enabled;
	cfg.save();
	emit statusChanged();
}

void ReplayController::onPlaybackEnded()
{
	const ReplayEvent *event = findEvent(playingId);
	const int count = event ? (int)event->angles.size() : 0;

	OBSDataAutoRelease data = obs_data_create();
	obs_data_set_string(data, "id", playingId.c_str());
	emitVendor("PlaybackEnded", data);

	if (cfg.cycleAngles && count > 1 && cycleShown < count) {
		const int next = (cycleStart + cycleShown) % count;
		cycleShown++;
		angleIndex = next;
		PlayerRef ref = acquirePlayer(false);
		if (ref.player) {
			ref.player->setAngle(next);
			ref.player->restart();
		}
		emit playbackUpdated();
		return;
	}

	while (!queue.isEmpty()) {
		const ReplayEvent *nextEvent = findEvent(queue.takeFirst().toStdString());
		if (nextEvent) {
			startEvent(*nextEvent, angleIndex, true, true);
			return;
		}
	}

	if (highlightSession) {
		/* loop the reel through the break; it picks up replays added since */
		const QStringList ids = reelIds(highlightReel);
		if (cfg.loopHighlights && !ids.isEmpty()) {
			playReelFrom(ids, 0);
			return;
		}
	} else if (cfg.autoPlayNext) {
		int idx = indexOfEvent(playingId);
		if (idx >= 0 && idx + 1 < (int)eventList.size()) {
			startEvent(eventList[(size_t)idx + 1], angleIndex, true, true);
			return;
		}
	}

	if (cfg.returnToLive)
		returnToLive();
}

void ReplayController::poll()
{
	PlayerStatus status;
	{
		PlayerRef ref = acquirePlayer(false);
		if (ref.player)
			status = ref.player->status();
	}

	if (pendingLiveScene) {
		if (status.framesShown > 0 || status.state == PlayerState::Error || nowUsec() > pendingLiveDeadline) {
			OBSSourceAutoRelease scene = obs_weak_source_get_source(pendingLiveScene);
			pendingLiveScene = nullptr;
			if (scene)
				goLiveNow(scene);
		}
	}

	const PlayerState previous = lastStatus.state;
	const bool changed = status.state != lastStatus.state || status.eventId != lastStatus.eventId ||
			     status.angle != lastStatus.angle || status.speed != lastStatus.speed ||
			     status.duration != lastStatus.duration || status.angleLabels != lastStatus.angleLabels ||
			     std::fabs(status.position - lastStatus.position) > 0.005;
	lastStatus = status;

	if (status.state == PlayerState::Error && previous != PlayerState::Error && !status.error.empty())
		say(QString(text("Msg.PlaybackFailed")).arg(qstr(status.error)), true);

	if (status.state == PlayerState::Ended && previous != PlayerState::Ended && !playingId.empty() &&
	    status.eventId == playingId)
		onPlaybackEnded();

	if (changed)
		emit playbackUpdated();

	/* websocket snapshot */
	OBSDataAutoRelease snap = obs_data_create();
	obs_data_set_bool(snap, "armed", engine.armed());
	obs_data_set_int(snap, "pending_replays", pendingClips);
	obs_data_set_bool(snap, "mark_in_set", markInTime != 0);
	if (markInTime)
		obs_data_set_double(snap, "mark_in_age", (double)(nowUsec() - markInTime) / 1e6);
	obs_data_set_string(snap, "selected_event", selectedId.c_str());

	OBSDataArrayAutoRelease angles = obs_data_array_create();
	for (const AngleStatus &a : engine.status()) {
		OBSDataAutoRelease item = obs_data_create();
		obs_data_set_string(item, "label", a.label.c_str());
		obs_data_set_string(item, "source", a.sourceName.c_str());
		obs_data_set_bool(item, "recording", a.running);
		obs_data_set_string(item, "state", a.state.c_str());
		obs_data_set_double(item, "buffered_seconds", a.bufferedSeconds);
		obs_data_array_push_back(angles, item);
	}
	obs_data_set_array(snap, "angles", angles);

	OBSDataAutoRelease playback = obs_data_create();
	obs_data_set_string(playback, "state", playerStateName(status.state));
	obs_data_set_string(playback, "event_id", status.eventId.c_str());
	obs_data_set_string(playback, "event_name", status.eventName.c_str());
	obs_data_set_int(playback, "angle", status.angle + 1);
	obs_data_set_string(
		playback, "angle_label",
		status.angle < (int)status.angleLabels.size() ? status.angleLabels[(size_t)status.angle].c_str() : "");
	obs_data_set_int(playback, "angle_count", (long long)status.angleLabels.size());
	obs_data_set_double(playback, "speed", status.state == PlayerState::Idle ? speedValue : status.speed);
	obs_data_set_double(playback, "position", status.position);
	obs_data_set_double(playback, "duration", status.duration);
	obs_data_set_bool(playback, "cued", cued);
	obs_data_set_bool(playback, "highlights", highlightsPlaying());
	obs_data_set_obj(snap, "playback", playback);

	OBSDataAutoRelease options = obs_data_create();
	obs_data_set_bool(options, "auto_play_next", cfg.autoPlayNext);
	obs_data_set_bool(options, "cycle_angles", cfg.cycleAngles);
	obs_data_set_bool(options, "return_to_live", cfg.returnToLive);
	obs_data_set_bool(options, "loop_highlights", cfg.loopHighlights);
	obs_data_set_obj(snap, "options", options);

	const Sport &s = sport();
	OBSDataAutoRelease match = obs_data_create();
	obs_data_set_string(match, "sport", s.id);
	obs_data_set_int(match, "phase", cfg.matchPhase);
	obs_data_set_string(match, "phase_name", phaseName(s, cfg.matchPhase).c_str());
	obs_data_set_bool(match, "in_break", isBreakPhase(s, cfg.matchPhase));
	obs_data_set_obj(snap, "match", match);

	std::lock_guard<std::mutex> lock(snapshotMutex);
	statusData = snap.Get();
	obs_data_addref(statusData);
}

void ReplayController::rebuildEventsSnapshot()
{
	OBSDataAutoRelease snap = obs_data_create();
	OBSDataArrayAutoRelease list = obs_data_array_create();
	for (size_t i = 0; i < eventList.size(); i++) {
		OBSDataAutoRelease item = eventToData(eventList[i]);
		obs_data_set_int(item, "index", (long long)i + 1);
		obs_data_array_push_back(list, item);
	}
	obs_data_set_array(snap, "events", list);
	obs_data_set_string(snap, "selected_event", selectedId.c_str());

	{
		std::lock_guard<std::mutex> lock(snapshotMutex);
		eventsData = snap.Get();
		obs_data_addref(eventsData);
	}
	rebuildMatchSnapshot(); /* reel contents follow the events */
}

void ReplayController::rebuildMatchSnapshot()
{
	const Sport &s = sport();
	OBSDataAutoRelease snap = obs_data_create();
	obs_data_set_string(snap, "sport", s.id);
	obs_data_set_string(snap, "sport_name", text(s.nameKey));
	obs_data_set_string(snap, "match_id", cfg.matchId.c_str());
	obs_data_set_int(snap, "phase", cfg.matchPhase);
	obs_data_set_string(snap, "phase_name", phaseName(s, cfg.matchPhase).c_str());
	obs_data_set_int(snap, "period", periodOfPhase(cfg.matchPhase));
	obs_data_set_bool(snap, "in_break", isBreakPhase(s, cfg.matchPhase));
	obs_data_set_int(snap, "current_reel", currentReel());
	obs_data_set_bool(snap, "loop_highlights", cfg.loopHighlights);

	OBSDataArrayAutoRelease phases = obs_data_array_create();
	for (int phase = 0; s.periods && phase < phaseCount(s); phase++) {
		OBSDataAutoRelease item = obs_data_create();
		obs_data_set_int(item, "phase", phase);
		obs_data_set_string(item, "name", phaseName(s, phase).c_str());
		obs_data_set_bool(item, "break", isBreakPhase(s, phase));
		obs_data_array_push_back(phases, item);
	}
	obs_data_set_array(snap, "phases", phases);

	OBSDataArrayAutoRelease reels = obs_data_array_create();
	for (int reel : reelsOf(s)) {
		const QStringList ids = reelIds(reel);
		double duration = 0;
		OBSDataArrayAutoRelease events = obs_data_array_create();
		for (const QString &id : ids) {
			const ReplayEvent *e = findEvent(id.toStdString());
			duration += e ? e->duration : 0;
			OBSDataAutoRelease item = obs_data_create();
			obs_data_set_string(item, "id", id.toUtf8().constData());
			obs_data_array_push_back(events, item);
		}
		OBSDataAutoRelease item = obs_data_create();
		obs_data_set_int(item, "reel", reel);
		obs_data_set_string(item, "name", reelName(s, reel).c_str());
		obs_data_set_int(item, "count", ids.size());
		obs_data_set_double(item, "duration", duration);
		obs_data_set_array(item, "events", events);
		obs_data_array_push_back(reels, item);
	}
	obs_data_set_array(snap, "reels", reels);

	std::lock_guard<std::mutex> lock(snapshotMutex);
	matchData = snap.Get();
	obs_data_addref(matchData);
}

obs_data_t *ReplayController::statusSnapshot()
{
	std::lock_guard<std::mutex> lock(snapshotMutex);
	obs_data_addref(statusData);
	return statusData;
}

obs_data_t *ReplayController::eventsSnapshot()
{
	std::lock_guard<std::mutex> lock(snapshotMutex);
	obs_data_addref(eventsData);
	return eventsData;
}

obs_data_t *ReplayController::matchSnapshot()
{
	std::lock_guard<std::mutex> lock(snapshotMutex);
	obs_data_addref(matchData);
	return matchData;
}

void ReplayController::emitVendor(const char *name, obs_data_t *data)
{
	emitVendorEvent(name, data);
}

} // namespace rc
