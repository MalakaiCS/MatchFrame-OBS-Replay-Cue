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

#pragma once

#include "capture-engine.hpp"
#include "clip-writer.hpp"
#include "replay-config.hpp"
#include "replay-event.hpp"
#include "replay-player.hpp"
#include "sport.hpp"

#include <obs.hpp>
#include <obs-frontend-api.h>

#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

class QFileSystemWatcher;
class QTimer;

namespace rc {

struct HotkeyBinding;

/*
 * Owns the capture engine, the event list and the playback session. Lives on
 * the Qt UI thread; hotkeys and websocket requests are marshalled onto it.
 */
class ReplayController : public QObject {
	Q_OBJECT

public:
	explicit ReplayController(QObject *parent = nullptr);
	~ReplayController() override;

	void shutdown();

	const Config &config() const { return cfg; }
	void applyConfig(const Config &updated);

	const std::vector<ReplayEvent> &events() const { return eventList; }
	const ReplayEvent *findEvent(const std::string &id) const;
	int indexOfEvent(const std::string &id) const;
	std::string selectedEventId() const { return selectedId; }
	std::string playingEventId() const { return playingId; }

	bool armed() const { return engine.armed(); }
	std::vector<AngleStatus> captureStatus() const { return engine.status(); }
	PlayerStatus playerStatus() const { return lastStatus; }
	int64_t markInUsec() const { return markInTime; }
	int pendingClipCount() const { return pendingClips; }
	int preferredAngle() const { return angleIndex; }
	double currentSpeed() const { return speedValue; }
	bool ffmpegOk() const;
	QString problem() const { return ffmpegProblem; }

	/* angle labels to show on the angle buttons right now */
	std::vector<std::string> activeAngleLabels() const;

	/* match and highlights */
	const Sport &sport() const { return findSport(cfg.sport); }
	int matchPhase() const { return cfg.matchPhase; }
	int currentReel() const { return reelForPhase(sport(), cfg.matchPhase); }
	bool inReel(const ReplayEvent &event, int reel) const;
	QStringList reelIds(int reel) const; /* the reel's highlights, oldest first */
	bool highlightsPlaying() const { return highlightSession && !playingId.empty(); }
	int playingReel() const { return highlightReel; }

	/* thread-safe copies for obs-websocket (caller releases) */
	obs_data_t *statusSnapshot();
	obs_data_t *eventsSnapshot();
	obs_data_t *matchSnapshot();

public slots:
	void toggleArm();
	void arm();
	void disarm();
	void markIn();
	void markOut();
	void clearMarks();
	void instantReplay(bool play, int seconds = 0);

	void selectEvent(const QString &id);
	void selectRelative(int delta);
	void cueEvent(const QString &id);
	void cueSelected();
	void playEvent(const QString &id, int angle = -1);
	void playSelected();
	void playLatest();
	void playNextEvent();
	void playPreviousEvent();
	void playQueue(const QStringList &ids);

	void togglePause();
	void pause();
	void resume();
	void restart();
	void returnToLive();

	void setAngle(int angle);
	void nextAngle();
	void prevAngle();
	void setSpeed(double speed);
	void seek(double position);
	void step(int frames);

	void setAutoPlayNext(bool enabled);
	void setCycleAngles(bool enabled);
	void setReturnToLive(bool enabled);

	void deleteEvents(const QStringList &ids);
	void renameEvent(const QString &id, const QString &name);
	void setEventTag(const QString &id, const QString &tag);
	void toggleFavorite(const QString &id);
	void exportEvent(const QString &id, int angle, bool trimToMarks);
	void refreshEvents();

	void setMatchPhase(int phase);
	void nextMatchPhase();
	void newMatch();
	void playHighlights(int reel = -1); /* -1: currentReel() */
	void setHighlightsLoop(bool enabled);
	void setHighlight(const QString &id, bool on);
	void toggleHighlight(const QString &id);
	void setEventPeriod(const QString &id, int period);

signals:
	void eventsChanged();
	void selectionChanged(const QString &id);
	void statusChanged();
	void playbackUpdated();
	void matchChanged();
	void message(const QString &text, bool error);

private:
	static void frontendEvent(enum obs_frontend_event event, void *data);
	static void frontendSave(obs_data_t *data, bool saving, void *param);
	void onFrontendEvent(enum obs_frontend_event event);

	void initialize();
	void registerHotkeys();
	void loadHotkeys();
	void saveHotkeys();

	void poll();
	void onPlaybackEnded();
	void createEvent(int64_t inUsec, int64_t outUsec, bool play);
	void onClipWritten(ClipResult result);
	void onScanFinished(std::vector<ReplayEvent> scanned);
	void updateWatcher();
	void importReplayBufferSave();

	struct PlayerRef {
		OBSSourceAutoRelease source;
		ReplayPlayer *player = nullptr;
	};
	/* the player stays valid for as long as the PlayerRef lives */
	PlayerRef acquirePlayer(bool create);
	bool ensureScene(const std::string &name, OBSSourceAutoRelease &scene, obs_source_t *player, bool create);
	PlayRequest buildRequest(const ReplayEvent &event, int angle, bool autoplay) const;
	void startEvent(const ReplayEvent &event, int angle, bool autoplay, bool goLive);
	void playReelFrom(const QStringList &ids, int index);
	void matchUpdated();
	void goLiveNow(obs_source_t *scene);
	void switchProgram(obs_source_t *scene, const std::string &transitionName);
	void restoreTransition();
	/* the scene the current playback goes out on: the replay scene, or the
	 * highlights scene while a highlights reel plays */
	std::string sessionSceneName() const;
	bool isReplayScene(obs_source_t *scene) const;
	bool replaySceneIsLive();
	ReplayEvent *findEventMutable(const std::string &id);
	bool persist(ReplayEvent &event);
	void adoptIntoMatch(ReplayEvent &event);
	void emitVendor(const char *name, obs_data_t *data);
	void rebuildEventsSnapshot();
	void rebuildMatchSnapshot();
	void say(const QString &text, bool error = false);

	Config cfg;
	CaptureEngine engine;
	std::unique_ptr<ClipWriter> writer;

	std::vector<ReplayEvent> eventList;
	std::set<std::string> recentWrites;
	std::string selectedId;
	int nextNumber = 1;
	int pendingClips = 0;
	int64_t markInTime = 0;

	/* playback session */
	std::string playingId;
	QStringList queue;
	int angleIndex = 0;
	double speedValue = 1.0;
	int cycleShown = 0;
	int cycleStart = 0;
	bool cued = false;
	PlayerStatus lastStatus;
	OBSWeakSourceAutoRelease playerWeak;
	OBSWeakSourceAutoRelease previousScene;
	OBSWeakSourceAutoRelease savedTransition;
	bool transitionOverridden = false;
	bool stopAfterTransition = false;
	OBSWeakSourceAutoRelease pendingLiveScene;
	int64_t pendingLiveDeadline = 0;
	bool highlightSession = false;
	int highlightReel = 0;

	/* folder scanning */
	ProbeCache probeCache;
	std::thread scanThread;
	bool scanning = false;
	bool rescanQueued = false;
	QFileSystemWatcher *watcher = nullptr;
	QTimer *rescanTimer = nullptr;
	QTimer *pollTimer = nullptr;
	QTimer *tickTimer = nullptr;

	/* hotkeys */
	std::vector<std::unique_ptr<HotkeyBinding>> hotkeys;

	/* websocket snapshots */
	std::mutex snapshotMutex;
	OBSDataAutoRelease statusData;
	OBSDataAutoRelease eventsData;
	OBSDataAutoRelease matchData;

	QString ffmpegProblem;
	bool initialized = false;
	bool wasArmedBeforeCollectionChange = false;
	bool shuttingDown = false;
};

} // namespace rc
