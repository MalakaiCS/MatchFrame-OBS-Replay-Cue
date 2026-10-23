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

#include "replay-config.hpp"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTableWidget;

namespace rc {

class SettingsDialog : public QDialog {
	Q_OBJECT

public:
	SettingsDialog(const Config &config, QWidget *parent = nullptr);

	Config result() const;

private:
	QWidget *buildCamerasTab();
	QWidget *buildCaptureTab();
	QWidget *buildReplaysTab();
	QWidget *buildPlaybackTab();
	QWidget *buildSportTab();

	void addAngleRow(const AngleSetting &angle);
	void addAllCameras();
	void moveSelectedRow(int delta);
	void updateMemoryEstimate();
	QStringList videoSourceNames() const;

	Config initial;

	QTableWidget *angleTable = nullptr;

	QCheckBox *armOnStartup = nullptr;
	QSpinBox *bufferSeconds = nullptr;
	QComboBox *encoder = nullptr;
	QSpinBox *bitrate = nullptr;
	QSpinBox *keyint = nullptr;
	QComboBox *maxHeight = nullptr;
	QComboBox *audioTrack = nullptr;
	QCheckBox *captureHdr = nullptr;
	QLabel *memoryLabel = nullptr;

	QLineEdit *folder = nullptr;
	QCheckBox *watchFolder = nullptr;
	QSpinBox *instantSeconds = nullptr;
	QSpinBox *tailMs = nullptr;
	QSpinBox *latencyMs = nullptr;
	QCheckBox *importReplayBuffer = nullptr;
	QLineEdit *exportFolder = nullptr;
	QLineEdit *tags = nullptr;

	QLineEdit *sceneName = nullptr;
	QLineEdit *playerName = nullptr;
	QComboBox *replayTransition = nullptr;
	QComboBox *returnTransition = nullptr;
	QComboBox *liveScene = nullptr;
	QComboBox *defaultSpeed = nullptr;
	QCheckBox *autoPlayNext = nullptr;
	QCheckBox *cycleAngles = nullptr;
	QCheckBox *returnToLive = nullptr;
	QCheckBox *playAudio = nullptr;
	QComboBox *slowMotionAudio = nullptr;
	QCheckBox *hwDecode = nullptr;
	QCheckBox *startCuedOnLive = nullptr;

	QComboBox *sport = nullptr;
	QCheckBox *highlightNewReplays = nullptr;
	QLineEdit *highlightsScene = nullptr;
	QComboBox *highlightsSpeed = nullptr;
	QCheckBox *loopHighlights = nullptr;
	QCheckBox *autoHighlights = nullptr;
	QSpinBox *autoHighlightsDelay = nullptr;
};

} // namespace rc
