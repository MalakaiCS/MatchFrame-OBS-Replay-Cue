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

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;
class QToolButton;

namespace rc {

class ReplayController;

/* The dock's match bar: the open match ("Match 1: Leeds vs York"), the
 * current period or break, and a button that moves the match on (New match
 * -> 1st Half -> Half Time -> 2nd Half -> Full Time). Its menu starts, edits
 * and reopens matches. Hidden when no sport is chosen. */
class MatchBar : public QWidget {
	Q_OBJECT

public:
	MatchBar(ReplayController *controller, QWidget *parent = nullptr);

private slots:
	void refresh();

private:
	void advance();
	void fillMenu();
	void startNewMatch();
	void editMatch();

	ReplayController *controller;
	QLabel *label;
	QPushButton *nextButton;
	QToolButton *menuButton;
};

/* Saves a PNG of the New match dialog, filled in (SaveUiSnapshot, for docs). */
void saveMatchDialogSnapshot(ReplayController *controller, const QString &path);

/* The "Highlights" tab: the replays of one break's reel (Half Time, Full
 * Time...), ticked to include them, and the controls to play the reel. */
class HighlightsTab : public QWidget {
	Q_OBJECT

public:
	HighlightsTab(ReplayController *controller, QWidget *parent = nullptr);

private slots:
	void rebuildReels();
	void rebuildList();
	void updateState();

private:
	int selectedReel() const;

	ReplayController *controller;
	QComboBox *reelCombo;
	QTableWidget *table;
	QLabel *summaryLabel;
	QLabel *hintLabel;
	QPushButton *playButton;
	QCheckBox *loopCheck;
	QPushButton *exportButton;

	std::string sportId;
	std::string lastPlaying;
	int includedCount = 0;
	double includedDuration = 0;
	bool updating = false;
};

} // namespace rc
