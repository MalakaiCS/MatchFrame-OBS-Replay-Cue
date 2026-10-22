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

#include <QWidget>

#include <string>
#include <vector>

class QButtonGroup;
class QCheckBox;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QSlider;
class QTabWidget;
class QTableWidget;
class QTimer;
class QToolButton;

namespace rc {

class HighlightsTab;
class MatchBar;
class ReplayController;

/* The "Replay Cue" dock: capture status, marking, the event list and the
 * replay transport (angles, speed, scrubbing). */
class CueDock : public QWidget {
	Q_OBJECT

public:
	CueDock(ReplayController *controller, QWidget *parent = nullptr);

private slots:
	void rebuildEvents();
	void syncSelection(const QString &id);
	void updateCapture();
	void updatePlayback();
	void showMessage(const QString &text, bool error);
	void onTableSelection();
	void onTableContextMenu(const QPoint &pos);
	void openSettings();

private:
	QStringList selectedIds() const;
	QString currentId() const;
	void rebuildAngleButtons(const std::vector<std::string> &labels);
	void renameSelected();
	void deleteSelected();
	void exportSelected(int angle, bool trim);
	void toggleHighlightSelected();
	void onMatchChanged();

	ReplayController *controller;

	QPushButton *armButton;
	QLabel *captureLabel;
	QToolButton *settingsButton;
	QPushButton *markInButton;
	QPushButton *markOutButton;
	QPushButton *instantButton;
	QPushButton *instantPlayButton;
	QLabel *markLabel;
	MatchBar *matchBar;
	QTabWidget *tabs;
	QTableWidget *table;
	HighlightsTab *highlightsTab;
	QHBoxLayout *angleLayout;
	QButtonGroup *angleGroup;
	std::vector<std::string> angleLabels;
	QButtonGroup *speedGroup;
	QSlider *speedSlider;
	QLabel *speedLabel;
	QPushButton *prevButton;
	QPushButton *stepBackButton;
	QPushButton *playButton;
	QPushButton *stepForwardButton;
	QPushButton *nextButton;
	QPushButton *cueButton;
	QPushButton *liveButton;
	QSlider *positionSlider;
	QLabel *timeLabel;
	QLabel *nowPlayingLabel;
	QCheckBox *autoNextCheck;
	QCheckBox *cycleCheck;
	QCheckBox *returnCheck;
	QLabel *messageLabel;
	QTimer *messageTimer;

	std::string lastPlaying;
	bool scrubbing = false;
	bool updatingTable = false;
};

/* Renders the dock and every settings tab to PNG files in `folder` (used for
 * documentation screenshots via the SaveUiSnapshot websocket request). */
void saveUiSnapshots(ReplayController *controller, const QString &folder);

} // namespace rc
