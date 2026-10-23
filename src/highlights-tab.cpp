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

#include "highlights-tab.hpp"
#include "rc-common.hpp"
#include "replay-controller.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace rc {

namespace {

QString T(const char *key)
{
	return QString::fromUtf8(text(key));
}

QString qs(const std::string &s)
{
	return QString::fromStdString(s);
}

enum Column { ColUse, ColNumber, ColName, ColTag, ColPeriod, ColLength, ColCount };
constexpr int kIdRole = Qt::UserRole + 1;

/* New match / Edit match: the match name and the two teams. */
class MatchDialog : public QDialog {
public:
	MatchDialog(const QString &title, const MatchInfo &match, const QString &warning, const QString &okText,
		    QWidget *parent)
		: QDialog(parent)
	{
		setWindowTitle(title);
		auto *root = new QVBoxLayout(this);
		if (!warning.isEmpty()) {
			auto *note = new QLabel(warning);
			note->setWordWrap(true);
			note->setStyleSheet("color: #fb8c00;");
			root->addWidget(note);
		}

		auto *form = new QFormLayout();
		nameEdit = new QLineEdit(qs(match.name));
		nameEdit->setPlaceholderText(T("Match.NamePlaceholder"));
		form->addRow(T("Match.Name"), nameEdit);
		auto *teams = new QHBoxLayout();
		homeEdit = new QLineEdit(qs(match.home));
		homeEdit->setPlaceholderText(T("Match.Home"));
		awayEdit = new QLineEdit(qs(match.away));
		awayEdit->setPlaceholderText(T("Match.Away"));
		teams->addWidget(homeEdit, 1);
		teams->addWidget(new QLabel(T("Match.Versus")));
		teams->addWidget(awayEdit, 1);
		form->addRow(T("Match.Teams"), teams);
		root->addLayout(form);

		folderLabel = new QLabel();
		folderLabel->setWordWrap(true);
		folderLabel->setTextFormat(Qt::PlainText);
		root->addWidget(folderLabel);

		auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
		buttons->button(QDialogButtonBox::Ok)->setText(okText);
		root->addWidget(buttons);
		connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
		connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
		for (QLineEdit *edit : {nameEdit, homeEdit, awayEdit})
			connect(edit, &QLineEdit::textChanged, this, [this]() { updateFolder(); });
		updateFolder();
		nameEdit->selectAll();
		nameEdit->setFocus();
		setMinimumWidth(420);
	}

	QString name() const { return nameEdit->text().trimmed(); }
	QString home() const { return homeEdit->text().trimmed(); }
	QString away() const { return awayEdit->text().trimmed(); }

private:
	void updateFolder()
	{
		MatchInfo m;
		m.name = name().toStdString();
		m.home = home().toStdString();
		m.away = away().toStdString();
		if (m.name.empty() && m.home.empty() && m.away.empty())
			m.name = nameEdit->placeholderText().toStdString();
		folderLabel->setText(QString(T("Match.FolderPreview")).arg(qs(matchFolderName(m))));
	}

	QLineEdit *nameEdit;
	QLineEdit *homeEdit;
	QLineEdit *awayEdit;
	QLabel *folderLabel;
};

} // namespace

/* ------------------------------------------------------------------------- */
/* Match bar                                                                 */

MatchBar::MatchBar(ReplayController *c, QWidget *parent) : QWidget(parent), controller(c)
{
	auto *row = new QHBoxLayout(this);
	row->setContentsMargins(0, 0, 0, 0);
	label = new QLabel();
	label->setTextFormat(Qt::RichText);
	/* a long match title must not widen the dock */
	label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	label->setMinimumWidth(60);
	nextButton = new QPushButton();
	menuButton = new QToolButton();
	menuButton->setText(QString::fromUtf8("…"));
	menuButton->setToolTip(T("Match.MenuTip"));
	menuButton->setPopupMode(QToolButton::InstantPopup);
	menuButton->setMenu(new QMenu(menuButton));
	row->addWidget(label, 1);
	row->addWidget(nextButton);
	row->addWidget(menuButton);

	connect(nextButton, &QPushButton::clicked, this, &MatchBar::advance);
	connect(menuButton->menu(), &QMenu::aboutToShow, this, &MatchBar::fillMenu);
	connect(controller, &ReplayController::matchChanged, this, &MatchBar::refresh);
	connect(controller, &ReplayController::statusChanged, this, &MatchBar::refresh);
	refresh();
}

void MatchBar::refresh()
{
	const Sport &sport = controller->sport();
	setVisible(sport.periods > 0);
	if (sport.periods == 0)
		return;

	const int phase = controller->matchPhase();
	const bool open = controller->matchOpen();
	const QString phaseText = qs(phaseName(sport, phase)).toHtmlEscaped();
	const QString title = open ? qs(matchTitle(controller->openMatch())) : T(sport.nameKey);
	label->setText(QString("<b>%1</b> &nbsp;&middot;&nbsp; %2")
			       .arg(title.toHtmlEscaped(),
				    isBreakPhase(sport, phase)
					    ? QString("<span style='color:#fb8c00'><b>%1</b></span>").arg(phaseText)
					    : phaseText));
	label->setToolTip(
		open ? QString(T("Match.FolderTip"))
				.arg(title, T(sport.nameKey),
				     qs(controller->openMatch().folder.empty() ? controller->config().folder
									       : controller->openMatch().folder))
		     : T("Match.NoMatchTip"));

	if (isFullTime(sport, phase) || (!open && phase == 0)) {
		nextButton->setText(T("Match.NewMatchMenu"));
		nextButton->setToolTip(T("Match.NewMatchTip"));
	} else if (phase % 2) {
		/* a period is in play: the button calls the break (or full time) */
		nextButton->setText(qs(phaseName(sport, phase + 1)));
		nextButton->setToolTip(
			QString(T("Match.EndTip")).arg(qs(phaseName(sport, phase)), qs(phaseName(sport, phase + 1))));
	} else {
		nextButton->setText(QString(T("Match.Start")).arg(qs(phaseName(sport, phase + 1))));
		nextButton->setToolTip(T("Match.StartTip"));
	}
}

void MatchBar::advance()
{
	if (isFullTime(controller->sport(), controller->matchPhase()) ||
	    (!controller->matchOpen() && controller->matchPhase() == 0))
		startNewMatch();
	else
		controller->nextMatchPhase();
}

void MatchBar::fillMenu()
{
	QMenu *menu = menuButton->menu();
	menu->clear();
	const Sport &sport = controller->sport();
	for (int phase = 0; phase < phaseCount(sport); phase++) {
		QAction *action = menu->addAction(qs(phaseName(sport, phase)));
		action->setCheckable(true);
		action->setChecked(phase == controller->matchPhase());
		connect(action, &QAction::triggered, this, [this, phase]() { controller->setMatchPhase(phase); });
	}
	menu->addSeparator();
	connect(menu->addAction(T("Match.NewMatchMenu")), &QAction::triggered, this, &MatchBar::startNewMatch);
	QAction *edit = menu->addAction(T("Match.EditMatchMenu"));
	edit->setEnabled(controller->matchOpen());
	connect(edit, &QAction::triggered, this, &MatchBar::editMatch);

	QMenu *openMenu = menu->addMenu(T("Match.OpenMatchMenu"));
	const std::vector<MatchInfo> matches = controller->matches();
	for (const MatchInfo &m : matches) {
		QAction *action = openMenu->addAction(
			QString::fromUtf8("%1  ·  %2").arg(qs(matchTitle(m)), qs(phaseName(sport, m.phase))));
		action->setCheckable(true);
		action->setChecked(m.id == controller->config().matchId);
		const QString id = qs(m.id);
		connect(action, &QAction::triggered, this, [this, id]() { controller->openMatch(id); });
	}
	if (matches.empty())
		openMenu->addAction(T("Match.NoMatches"))->setEnabled(false);

	menu->addSeparator();
	QAction *all = menu->addAction(T("Match.ShowAll"));
	all->setCheckable(true);
	all->setChecked(controller->config().showAllMatches);
	all->setToolTip(T("Match.ShowAllTip"));
	connect(all, &QAction::triggered, this, [this](bool on) { controller->setShowAllMatches(on); });
}

void MatchBar::startNewMatch()
{
	const Sport &sport = controller->sport();
	const int phase = controller->matchPhase();
	QString warning;
	if (controller->matchOpen() && phase > 0 && !isFullTime(sport, phase))
		warning = QString(T("Match.InProgress"))
				  .arg(qs(matchTitle(controller->openMatch())), qs(phaseName(sport, phase)));
	MatchInfo fresh;
	fresh.name = controller->defaultMatchName().toStdString();
	MatchDialog dialog(T("Match.NewMatch"), fresh, warning, T("Match.Create"), this);
	if (dialog.exec() == QDialog::Accepted)
		controller->newMatch(dialog.name(), dialog.home(), dialog.away());
}

void MatchBar::editMatch()
{
	if (!controller->matchOpen())
		return;
	MatchDialog dialog(T("Match.EditMatch"), controller->openMatch(), QString(), T("Match.Save"), this);
	if (dialog.exec() == QDialog::Accepted)
		controller->editMatch(dialog.name(), dialog.home(), dialog.away());
}

void saveMatchDialogSnapshot(ReplayController *controller, const QString &path)
{
	MatchInfo example;
	example.name = controller->defaultMatchName().toStdString();
	example.home = "Leeds";
	example.away = "York";
	MatchDialog dialog(T("Match.NewMatch"), example, QString(), T("Match.Create"), nullptr);
	dialog.setAttribute(Qt::WA_DontShowOnScreen);
	dialog.show();
	dialog.grab().save(path);
	dialog.hide();
}

/* ------------------------------------------------------------------------- */
/* Highlights tab                                                            */

HighlightsTab::HighlightsTab(ReplayController *c, QWidget *parent) : QWidget(parent), controller(c)
{
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(0, 4, 0, 0);
	root->setSpacing(4);

	auto *reelRow = new QHBoxLayout();
	reelRow->addWidget(new QLabel(T("Highlights.Reel")));
	reelCombo = new QComboBox();
	reelCombo->setToolTip(T("Highlights.ReelTip"));
	reelRow->addWidget(reelCombo, 1);
	root->addLayout(reelRow);

	table = new QTableWidget(0, ColCount);
	table->setHorizontalHeaderLabels({QString::fromUtf8("✓"), T("Column.Number"), T("Column.Name"), T("Column.Tag"),
					  T("Column.Period"), T("Column.Length")});
	table->horizontalHeaderItem(ColUse)->setToolTip(T("Highlights.UseTip"));
	table->verticalHeader()->hide();
	table->setSelectionBehavior(QAbstractItemView::SelectRows);
	table->setSelectionMode(QAbstractItemView::SingleSelection);
	table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	table->setAlternatingRowColors(true);
	table->setWordWrap(false);
	table->setMinimumHeight(120);
	table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	table->horizontalHeader()->setSectionResizeMode(ColName, QHeaderView::Stretch);
	root->addWidget(table, 1);

	hintLabel = new QLabel();
	hintLabel->setWordWrap(true);
	root->addWidget(hintLabel);

	auto *playRow = new QHBoxLayout();
	playButton = new QPushButton(T("Highlights.Play"));
	playButton->setToolTip(T("Highlights.PlayTip"));
	playButton->setStyleSheet("QPushButton { font-weight: bold; }");
	loopCheck = new QCheckBox(T("Highlights.Loop"));
	loopCheck->setToolTip(T("Highlights.LoopTip"));
	exportButton = new QPushButton(T("Highlights.Export"));
	exportButton->setToolTip(T("Highlights.ExportTip"));
	summaryLabel = new QLabel();
	playRow->addWidget(playButton);
	playRow->addWidget(loopCheck);
	playRow->addWidget(exportButton);
	playRow->addStretch(1);
	playRow->addWidget(summaryLabel);
	root->addLayout(playRow);

	connect(reelCombo, &QComboBox::currentIndexChanged, this, &HighlightsTab::rebuildList);
	connect(playButton, &QPushButton::clicked, this, [this]() { controller->playHighlights(selectedReel()); });
	connect(loopCheck, &QCheckBox::clicked, this, [this](bool on) { controller->setHighlightsLoop(on); });
	connect(exportButton, &QPushButton::clicked, this, [this]() { controller->exportHighlights(selectedReel()); });
	connect(table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
		if (updating || item->column() != ColUse)
			return;
		const QString id = item->data(kIdRole).toString();
		const bool on = item->checkState() == Qt::Checked;
		/* the controller rebuilds this table: let the edit finish first */
		QMetaObject::invokeMethod(
			this, [this, id, on]() { controller->setHighlight(id, on); }, Qt::QueuedConnection);
	});
	connect(table, &QTableWidget::itemDoubleClicked, this, [this](QTableWidgetItem *item) {
		if (QTableWidgetItem *use = table->item(item->row(), ColUse))
			controller->playEvent(use->data(kIdRole).toString());
	});

	connect(controller, &ReplayController::eventsChanged, this, &HighlightsTab::rebuildList);
	connect(controller, &ReplayController::matchChanged, this, &HighlightsTab::rebuildReels);
	connect(controller, &ReplayController::statusChanged, this, [this]() {
		if (sportId != controller->sport().id)
			rebuildReels();
		updateState();
	});
	connect(controller, &ReplayController::playbackUpdated, this, &HighlightsTab::updateState);

	rebuildReels();
}

int HighlightsTab::selectedReel() const
{
	return reelCombo->currentIndex() >= 0 ? reelCombo->currentData().toInt() : controller->currentReel();
}

void HighlightsTab::rebuildReels()
{
	const Sport &sport = controller->sport();
	sportId = sport.id;
	{
		const QSignalBlocker block(reelCombo);
		reelCombo->clear();
		for (int reel : reelsOf(sport))
			reelCombo->addItem(qs(reelName(sport, reel)), reel);
		/* the match moved on: show the reel to get ready now */
		const int idx = reelCombo->findData(controller->currentReel());
		reelCombo->setCurrentIndex(idx >= 0 ? idx : reelCombo->count() - 1);
	}
	rebuildList();
}

void HighlightsTab::rebuildList()
{
	const Sport &sport = controller->sport();
	const int reel = selectedReel();
	const std::string playing = controller->playingEventId();
	lastPlaying = playing;

	std::vector<const ReplayEvent *> rows;
	for (const ReplayEvent &e : controller->events())
		if (controller->inReel(e, reel))
			rows.push_back(&e);

	updating = true;
	table->setRowCount((int)rows.size());
	table->setColumnHidden(ColPeriod, sport.periods == 0);
	includedCount = 0;
	includedDuration = 0;
	for (int row = 0; row < (int)rows.size(); row++) {
		const ReplayEvent &e = *rows[(size_t)row];
		if (e.highlight) {
			includedCount++;
			includedDuration += e.duration;
		}
		const QString cells[ColCount] = {
			QString(),
			e.number ? QString::number(e.number) : QString::fromUtf8("–"),
			(e.favorite ? QString::fromUtf8("★ ") : QString()) + qs(e.name),
			qs(e.tag),
			qs(periodName(sport, e.period)),
			qs(formatDuration(e.duration)),
		};
		for (int col = 0; col < ColCount; col++) {
			QTableWidgetItem *item = table->item(row, col);
			if (!item) {
				item = new QTableWidgetItem();
				table->setItem(row, col, item);
			}
			item->setText(cells[col]);
			QFont font = item->font();
			font.setBold(e.id == playing);
			item->setFont(font);
			if (col == ColUse) {
				item->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
				item->setCheckState(e.highlight ? Qt::Checked : Qt::Unchecked);
				item->setData(kIdRole, qs(e.id));
			}
		}
	}
	updating = false;

	if (!rows.empty())
		hintLabel->clear();
	else if (sport.periods == 0)
		hintLabel->setText(T("Highlights.EmptyNoSport"));
	else
		hintLabel->setText(T("Highlights.Empty"));
	hintLabel->setVisible(rows.empty());
	updateState();
}

void HighlightsTab::updateState()
{
	if (lastPlaying != controller->playingEventId()) {
		rebuildList(); /* bold the playing row; calls back here */
		return;
	}

	const Config &cfg = controller->config();
	loopCheck->setChecked(cfg.loopHighlights);
	playButton->setEnabled(includedCount > 0);
	exportButton->setEnabled(includedCount > 0);

	const int reel = selectedReel();
	if (controller->highlightsPlaying() && controller->playingReel() == reel) {
		const QStringList ids = controller->reelIds(reel);
		const int position = (int)ids.indexOf(qs(controller->playingEventId())) + 1;
		summaryLabel->setText(QString(T("Highlights.Playing")).arg(position).arg(ids.size()));
	} else {
		summaryLabel->setText(
			QString(T("Highlights.Summary")).arg(includedCount).arg(qs(formatDuration(includedDuration))));
	}
}

} // namespace rc
