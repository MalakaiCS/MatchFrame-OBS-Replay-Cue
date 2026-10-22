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

#include "cue-dock.hpp"
#include "rc-common.hpp"
#include "replay-controller.hpp"
#include "settings-dialog.hpp"

#include <QAction>
#include <QButtonGroup>
#include <QCheckBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QSlider>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <cmath>
#include <functional>
#include <set>

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

enum Column { ColNumber, ColName, ColTag, ColTime, ColLength, ColAngles, ColCount };
constexpr int kIdRole = Qt::UserRole + 1;
const double kSpeeds[] = {0.25, 0.5, 0.75, 1.0};

QString stateText(PlayerState state)
{
	switch (state) {
	case PlayerState::Loading:
		return T("State.Loading");
	case PlayerState::Playing:
		return T("State.Playing");
	case PlayerState::Paused:
		return T("State.Paused");
	case PlayerState::Ended:
		return T("State.Ended");
	case PlayerState::Error:
		return T("State.Error");
	case PlayerState::Idle:
		break;
	}
	return T("State.Idle");
}

QPushButton *transportButton(const QString &label, const QString &tip)
{
	auto *button = new QPushButton(label);
	button->setToolTip(tip);
	button->setMinimumWidth(32);
	return button;
}

} // namespace

CueDock::CueDock(ReplayController *c, QWidget *parent) : QWidget(parent), controller(c)
{
	setObjectName("ReplayCueDock");
	setMinimumWidth(380);

	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(4, 4, 4, 4);
	root->setSpacing(4);

	/* capture */
	auto *captureRow = new QHBoxLayout();
	armButton = new QPushButton(T("Dock.Arm"));
	armButton->setCheckable(true);
	armButton->setToolTip(T("Dock.ArmTip"));
	captureLabel = new QLabel();
	captureLabel->setTextFormat(Qt::RichText);
	captureLabel->setWordWrap(true);
	settingsButton = new QToolButton();
	settingsButton->setText(T("Dock.Settings"));
	settingsButton->setToolTip(T("Dock.SettingsTip"));
	captureRow->addWidget(armButton);
	captureRow->addWidget(captureLabel, 1);
	captureRow->addWidget(settingsButton);
	root->addLayout(captureRow);

	/* marking */
	auto *markRow = new QHBoxLayout();
	markInButton = new QPushButton(T("Dock.MarkIn"));
	markInButton->setToolTip(T("Dock.MarkInTip"));
	markOutButton = new QPushButton(T("Dock.MarkOut"));
	markOutButton->setToolTip(T("Dock.MarkOutTip"));
	instantButton = new QPushButton(T("Dock.Instant"));
	instantButton->setToolTip(T("Dock.InstantTip"));
	instantPlayButton = new QPushButton(T("Dock.InstantPlay"));
	instantPlayButton->setToolTip(T("Dock.InstantPlayTip"));
	markRow->addWidget(markInButton);
	markRow->addWidget(markOutButton);
	markRow->addWidget(instantButton);
	markRow->addWidget(instantPlayButton);
	root->addLayout(markRow);
	markLabel = new QLabel();
	root->addWidget(markLabel);

	/* events */
	table = new QTableWidget(0, ColCount);
	table->setHorizontalHeaderLabels({T("Column.Number"), T("Column.Name"), T("Column.Tag"), T("Column.Time"),
					  T("Column.Length"), T("Column.Angles")});
	table->verticalHeader()->hide();
	table->setSelectionBehavior(QAbstractItemView::SelectRows);
	table->setSelectionMode(QAbstractItemView::ExtendedSelection);
	table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	table->setContextMenuPolicy(Qt::CustomContextMenu);
	table->setAlternatingRowColors(true);
	table->setWordWrap(false);
	table->setMinimumHeight(120);
	table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	table->horizontalHeader()->setSectionResizeMode(ColName, QHeaderView::Stretch);
	root->addWidget(table, 1);

	nowPlayingLabel = new QLabel();
	nowPlayingLabel->setTextFormat(Qt::RichText);
	root->addWidget(nowPlayingLabel);

	/* angles */
	auto *angleRow = new QHBoxLayout();
	angleRow->addWidget(new QLabel(T("Dock.Angle")));
	angleLayout = new QHBoxLayout();
	angleLayout->setSpacing(2);
	angleRow->addLayout(angleLayout, 1);
	root->addLayout(angleRow);
	angleGroup = new QButtonGroup(this);
	angleGroup->setExclusive(true);

	/* speed */
	auto *speedRow = new QHBoxLayout();
	speedRow->addWidget(new QLabel(T("Dock.Speed")));
	speedGroup = new QButtonGroup(this);
	speedGroup->setExclusive(false);
	for (int i = 0; i < 4; i++) {
		auto *button = new QPushButton(QString("%1%").arg((int)(kSpeeds[i] * 100)));
		button->setCheckable(true);
		button->setMinimumWidth(40);
		speedGroup->addButton(button, i);
		speedRow->addWidget(button);
	}
	speedSlider = new QSlider(Qt::Horizontal);
	speedSlider->setRange(5, 200);
	speedSlider->setValue(100);
	speedRow->addWidget(speedSlider, 1);
	speedLabel = new QLabel("100%");
	speedLabel->setMinimumWidth(40);
	speedRow->addWidget(speedLabel);
	root->addLayout(speedRow);

	/* transport */
	auto *transportRow = new QHBoxLayout();
	prevButton = transportButton(QString::fromUtf8("⏮"), T("Dock.PrevTip"));
	stepBackButton = transportButton(QString::fromUtf8("◀|"), T("Dock.StepBackTip"));
	playButton = transportButton(QString::fromUtf8("▶"), T("Dock.PlayTip"));
	stepForwardButton = transportButton(QString::fromUtf8("|▶"), T("Dock.StepForwardTip"));
	nextButton = transportButton(QString::fromUtf8("⏭"), T("Dock.NextTip"));
	cueButton = transportButton(T("Dock.Cue"), T("Dock.CueTip"));
	liveButton = transportButton(T("Dock.Live"), T("Dock.LiveTip"));
	liveButton->setStyleSheet("QPushButton { font-weight: bold; }");
	for (QPushButton *b : {prevButton, stepBackButton, playButton, stepForwardButton, nextButton})
		transportRow->addWidget(b);
	transportRow->addStretch(1);
	transportRow->addWidget(cueButton);
	transportRow->addWidget(liveButton);
	root->addLayout(transportRow);

	auto *positionRow = new QHBoxLayout();
	positionSlider = new QSlider(Qt::Horizontal);
	positionSlider->setRange(0, 1000);
	timeLabel = new QLabel("00:00.00 / 00:00.00");
	positionRow->addWidget(positionSlider, 1);
	positionRow->addWidget(timeLabel);
	root->addLayout(positionRow);

	/* options */
	auto *optionsRow = new QHBoxLayout();
	autoNextCheck = new QCheckBox(T("Dock.AutoPlayNext"));
	autoNextCheck->setToolTip(T("Dock.AutoPlayNextTip"));
	cycleCheck = new QCheckBox(T("Dock.CycleAngles"));
	cycleCheck->setToolTip(T("Dock.CycleAnglesTip"));
	returnCheck = new QCheckBox(T("Dock.ReturnToLive"));
	returnCheck->setToolTip(T("Dock.ReturnToLiveTip"));
	optionsRow->setSpacing(20);
	optionsRow->addWidget(autoNextCheck);
	optionsRow->addWidget(cycleCheck);
	optionsRow->addWidget(returnCheck);
	optionsRow->addStretch(1);
	for (QCheckBox *check : {autoNextCheck, cycleCheck, returnCheck})
		check->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
	root->addLayout(optionsRow);

	messageLabel = new QLabel();
	messageLabel->setWordWrap(true);
	root->addWidget(messageLabel);
	messageTimer = new QTimer(this);
	messageTimer->setSingleShot(true);
	connect(messageTimer, &QTimer::timeout, messageLabel, &QLabel::clear);

	/* wiring */
	connect(armButton, &QPushButton::clicked, this, [this]() { controller->toggleArm(); });
	connect(settingsButton, &QToolButton::clicked, this, &CueDock::openSettings);
	connect(markInButton, &QPushButton::clicked, this, [this]() { controller->markIn(); });
	connect(markOutButton, &QPushButton::clicked, this, [this]() { controller->markOut(); });
	connect(instantButton, &QPushButton::clicked, this, [this]() { controller->instantReplay(false); });
	connect(instantPlayButton, &QPushButton::clicked, this, [this]() { controller->instantReplay(true); });

	connect(table, &QTableWidget::itemSelectionChanged, this, &CueDock::onTableSelection);
	connect(table, &QTableWidget::customContextMenuRequested, this, &CueDock::onTableContextMenu);
	connect(table, &QTableWidget::itemDoubleClicked, this, [this](QTableWidgetItem *item) {
		QTableWidgetItem *idItem = table->item(item->row(), ColNumber);
		if (idItem)
			controller->playEvent(idItem->data(kIdRole).toString());
	});

	connect(angleGroup, &QButtonGroup::idClicked, this, [this](int id) { controller->setAngle(id); });
	connect(speedGroup, &QButtonGroup::idClicked, this, [this](int id) { controller->setSpeed(kSpeeds[id]); });
	connect(speedSlider, &QSlider::valueChanged, this, [this](int value) {
		if (speedSlider->isSliderDown())
			controller->setSpeed(value / 100.0);
	});
	connect(speedSlider, &QSlider::sliderReleased, this,
		[this]() { controller->setSpeed(speedSlider->value() / 100.0); });

	connect(prevButton, &QPushButton::clicked, this, [this]() { controller->playPreviousEvent(); });
	connect(nextButton, &QPushButton::clicked, this, [this]() { controller->playNextEvent(); });
	connect(stepBackButton, &QPushButton::clicked, this, [this]() { controller->step(-1); });
	connect(stepForwardButton, &QPushButton::clicked, this, [this]() { controller->step(1); });
	connect(playButton, &QPushButton::clicked, this, [this]() {
		const PlayerStatus st = controller->playerStatus();
		const bool loaded = st.state != PlayerState::Idle && !controller->playingEventId().empty();
		if (st.state == PlayerState::Playing)
			controller->pause();
		else if (loaded && controller->selectedEventId() == controller->playingEventId())
			controller->resume();
		else
			controller->playSelected();
	});
	connect(cueButton, &QPushButton::clicked, this, [this]() { controller->cueSelected(); });
	connect(liveButton, &QPushButton::clicked, this, [this]() { controller->returnToLive(); });

	connect(positionSlider, &QSlider::sliderPressed, this, [this]() { scrubbing = true; });
	connect(positionSlider, &QSlider::sliderMoved, this,
		[this](int value) { controller->seek(value / 1000.0 * controller->playerStatus().duration); });
	connect(positionSlider, &QSlider::sliderReleased, this, [this]() {
		controller->seek(positionSlider->value() / 1000.0 * controller->playerStatus().duration);
		scrubbing = false;
	});

	connect(autoNextCheck, &QCheckBox::clicked, this, [this](bool on) { controller->setAutoPlayNext(on); });
	connect(cycleCheck, &QCheckBox::clicked, this, [this](bool on) { controller->setCycleAngles(on); });
	connect(returnCheck, &QCheckBox::clicked, this, [this](bool on) { controller->setReturnToLive(on); });

	connect(controller, &ReplayController::eventsChanged, this, &CueDock::rebuildEvents);
	connect(controller, &ReplayController::selectionChanged, this, &CueDock::syncSelection);
	connect(controller, &ReplayController::statusChanged, this, &CueDock::updateCapture);
	connect(controller, &ReplayController::playbackUpdated, this, &CueDock::updatePlayback);
	connect(controller, &ReplayController::message, this, &CueDock::showMessage);

	/* keyboard control while the dock has focus (global keys: OBS hotkeys) */
	auto shortcut = [this](const QKeySequence &key, std::function<void()> fn) {
		auto *s = new QShortcut(key, this);
		s->setContext(Qt::WidgetWithChildrenShortcut);
		connect(s, &QShortcut::activated, this, fn);
	};
	shortcut(QKeySequence(Qt::Key_Space), [this]() { playButton->click(); });
	shortcut(QKeySequence(Qt::Key_Return), [this]() { controller->playSelected(); });
	shortcut(QKeySequence(Qt::Key_Enter), [this]() { controller->playSelected(); });
	shortcut(QKeySequence(Qt::Key_Escape), [this]() { controller->returnToLive(); });
	shortcut(QKeySequence(Qt::Key_Delete), [this]() { deleteSelected(); });
	shortcut(QKeySequence(Qt::Key_Backspace), [this]() { deleteSelected(); });
	shortcut(QKeySequence(Qt::Key_F2), [this]() { renameSelected(); });
	shortcut(QKeySequence(Qt::Key_Left), [this]() { controller->step(-1); });
	shortcut(QKeySequence(Qt::Key_Right), [this]() { controller->step(1); });
	shortcut(QKeySequence(Qt::Key_I), [this]() { controller->markIn(); });
	shortcut(QKeySequence(Qt::Key_O), [this]() { controller->markOut(); });
	for (int i = 0; i < kMaxAngles; i++)
		shortcut(QKeySequence(Qt::Key_1 + i), [this, i]() { controller->setAngle(i); });

	rebuildEvents();
	updateCapture();
	updatePlayback();
}

QStringList CueDock::selectedIds() const
{
	std::set<int> rows;
	for (QTableWidgetItem *item : table->selectedItems())
		rows.insert(item->row());
	QStringList ids;
	for (int row : rows)
		if (QTableWidgetItem *item = table->item(row, ColNumber))
			ids << item->data(kIdRole).toString();
	return ids;
}

QString CueDock::currentId() const
{
	int row = table->currentRow();
	if (row >= 0 && table->item(row, ColNumber) && table->item(row, ColNumber)->isSelected())
		return table->item(row, ColNumber)->data(kIdRole).toString();
	QStringList ids = selectedIds();
	return ids.isEmpty() ? QString() : ids.front();
}

void CueDock::rebuildEvents()
{
	updatingTable = true;
	const auto &events = controller->events();
	const std::string playing = controller->playingEventId();
	table->setRowCount((int)events.size());

	for (int row = 0; row < (int)events.size(); row++) {
		const ReplayEvent &e = events[(size_t)row];
		QStringList labels;
		QStringList files;
		for (const AngleClip &clip : e.angles) {
			labels << qs(clip.label);
			files << qs(clip.file);
		}

		const QString cells[ColCount] = {
			e.number ? QString::number(e.number) : QString::fromUtf8("–"),
			(e.favorite ? QString::fromUtf8("★ ") : QString()) + qs(e.name),
			qs(e.tag),
			qs(formatClock(e.created)),
			qs(formatDuration(e.duration)),
			QString::number(e.angles.size()),
		};
		for (int col = 0; col < ColCount; col++) {
			QTableWidgetItem *item = table->item(row, col);
			if (!item) {
				item = new QTableWidgetItem();
				table->setItem(row, col, item);
			}
			item->setText(cells[col]);
			item->setToolTip(labels.join(", ") + "\n" + files.join("\n"));
			QFont font = item->font();
			font.setBold(e.id == playing);
			item->setFont(font);
			if (col == ColNumber)
				item->setData(kIdRole, qs(e.id));
		}
	}
	updatingTable = false;
	syncSelection(qs(controller->selectedEventId()));
}

void CueDock::syncSelection(const QString &id)
{
	if (id.isEmpty())
		return;
	if (selectedIds().contains(id) && currentId() == id)
		return;
	for (int row = 0; row < table->rowCount(); row++) {
		QTableWidgetItem *item = table->item(row, ColNumber);
		if (item && item->data(kIdRole).toString() == id) {
			updatingTable = true;
			table->clearSelection();
			table->setCurrentCell(row, ColName);
			table->selectRow(row);
			table->scrollToItem(item);
			updatingTable = false;
			break;
		}
	}
	updatePlayback();
}

void CueDock::onTableSelection()
{
	if (updatingTable)
		return;
	const QString id = currentId();
	if (!id.isEmpty())
		controller->selectEvent(id);
}

void CueDock::updateCapture()
{
	const bool armed = controller->armed();
	armButton->setChecked(armed);
	armButton->setText(armed ? T("Dock.Armed") : T("Dock.Arm"));
	armButton->setStyleSheet(armed ? "QPushButton { background-color: #c62828; color: white; font-weight: bold; }"
				       : QString());

	QStringList chips;
	QStringList details;
	size_t bytes = 0;
	if (armed) {
		for (const AngleStatus &a : controller->captureStatus()) {
			const bool live = a.running && a.bufferedSeconds > 0;
			const char *color = live ? "#43a047" : a.running ? "#fdd835" : "#9e9e9e";
			const QString info = a.running ? QString("%1s").arg((int)a.bufferedSeconds) : qs(a.state);
			chips << QString("<span style='color:%1'>&#9679;</span>&nbsp;%2&nbsp;<small>%3</small>")
					 .arg(color, qs(a.label).toHtmlEscaped(), info.toHtmlEscaped());
			details << QString("%1: %2 (%3) %4x%5")
					   .arg(qs(a.label), qs(a.sourceName), qs(a.state))
					   .arg(a.width)
					   .arg(a.height);
			bytes += a.bytes;
		}
		details << QString(T("Dock.Memory")).arg(bytes / (1024.0 * 1024.0), 0, 'f', 0);
	} else {
		chips << T("Dock.NotArmed");
	}
	captureLabel->setText(chips.join("&nbsp;&nbsp; "));
	captureLabel->setToolTip(details.join("\n"));

	QStringList marks;
	if (controller->markInUsec())
		marks << QString(T("Dock.InSet")).arg((double)(nowUsec() - controller->markInUsec()) / 1e6, 0, 'f', 1);
	if (controller->pendingClipCount())
		marks << QString(T("Dock.Saving")).arg(controller->pendingClipCount());
	markLabel->setText(marks.join("   "));
	markLabel->setVisible(!marks.isEmpty());

	markInButton->setEnabled(armed);
	markOutButton->setEnabled(armed);
	instantButton->setEnabled(armed);
	instantPlayButton->setEnabled(armed);

	const Config &cfg = controller->config();
	autoNextCheck->setChecked(cfg.autoPlayNext);
	cycleCheck->setChecked(cfg.cycleAngles);
	returnCheck->setChecked(cfg.returnToLive);

	if (!controller->problem().isEmpty() && messageLabel->text().isEmpty())
		showMessage(controller->problem(), true);
}

void CueDock::rebuildAngleButtons(const std::vector<std::string> &labels)
{
	for (QAbstractButton *button : angleGroup->buttons()) {
		angleGroup->removeButton(button);
		button->deleteLater();
	}
	angleLabels = labels;
	for (size_t i = 0; i < labels.size(); i++) {
		auto *button = new QPushButton(QString("%1 %2").arg(i + 1).arg(qs(labels[i])));
		button->setCheckable(true);
		button->setToolTip(QString(T("Dock.AngleTip")).arg(i + 1));
		angleGroup->addButton(button, (int)i);
		angleLayout->addWidget(button);
	}
}

void CueDock::updatePlayback()
{
	const PlayerStatus st = controller->playerStatus();
	const bool loaded = st.state != PlayerState::Idle && !controller->playingEventId().empty();

	const auto labels = controller->activeAngleLabels();
	if (labels != angleLabels)
		rebuildAngleButtons(labels);
	const int angle = loaded ? st.angle : controller->preferredAngle();
	if (QAbstractButton *button = angleGroup->button(angle))
		button->setChecked(true);

	const double speed = loaded ? st.speed : controller->currentSpeed();
	for (int i = 0; i < 4; i++)
		speedGroup->button(i)->setChecked(std::fabs(kSpeeds[i] - speed) < 0.001);
	if (!speedSlider->isSliderDown()) {
		const QSignalBlocker block(speedSlider);
		speedSlider->setValue((int)std::lround(speed * 100));
	}
	speedLabel->setText(QString("%1%").arg((int)std::lround(speed * 100)));

	if (!scrubbing) {
		const QSignalBlocker block(positionSlider);
		positionSlider->setValue(st.duration > 0 ? (int)(st.position / st.duration * 1000) : 0);
	}
	positionSlider->setEnabled(loaded);
	timeLabel->setText(qs(formatDuration(st.position)) + " / " + qs(formatDuration(st.duration)));

	playButton->setText(st.state == PlayerState::Playing ? QString::fromUtf8("⏸") : QString::fromUtf8("▶"));
	liveButton->setEnabled(loaded);
	stepBackButton->setEnabled(loaded);
	stepForwardButton->setEnabled(loaded);

	if (loaded) {
		QString angleName = st.angle < (int)st.angleLabels.size() ? qs(st.angleLabels[(size_t)st.angle])
									  : QString();
		nowPlayingLabel->setText(QString("<b>%1</b> &nbsp;%2 &nbsp;&middot;&nbsp; %3")
						 .arg(stateText(st.state).toHtmlEscaped(),
						      qs(st.eventName).toHtmlEscaped(), angleName.toHtmlEscaped()));
	} else {
		nowPlayingLabel->setText(T("State.Ready"));
	}

	/* bold the playing row */
	if (lastPlaying != controller->playingEventId()) {
		lastPlaying = controller->playingEventId();
		rebuildEvents();
	}
}

void CueDock::showMessage(const QString &msg, bool error)
{
	messageLabel->setText(msg);
	messageLabel->setStyleSheet(error ? "color: #ef5350;" : QString());
	messageTimer->start(error ? 10000 : 5000);
}

void CueDock::onTableContextMenu(const QPoint &pos)
{
	const QStringList ids = selectedIds();
	if (ids.isEmpty())
		return;
	const ReplayEvent *event = controller->findEvent(ids.front().toStdString());
	if (!event)
		return;
	const QString first = ids.front();

	QMenu menu(this);
	auto add = [this](QMenu *m, const QString &label, std::function<void()> fn) {
		QAction *action = m->addAction(label);
		connect(action, &QAction::triggered, this, fn);
		return action;
	};

	add(&menu, ids.size() > 1 ? QString(T("Menu.PlayN")).arg(ids.size()) : T("Menu.Play"), [this, ids]() {
		if (ids.size() > 1)
			controller->playQueue(ids);
		else
			controller->playEvent(ids.front());
	});
	add(&menu, T("Menu.Cue"), [this, first]() { controller->cueEvent(first); });
	if (event->angles.size() > 1) {
		QMenu *angles = menu.addMenu(T("Menu.PlayAngle"));
		for (size_t i = 0; i < event->angles.size(); i++)
			add(angles, qs(event->angles[i].label),
			    [this, first, i]() { controller->playEvent(first, (int)i); });
	}
	menu.addSeparator();

	if (ids.size() == 1)
		add(&menu, T("Menu.Rename"), [this]() { renameSelected(); });
	QMenu *tags = menu.addMenu(T("Menu.Tag"));
	for (const std::string &tag : controller->config().tags)
		add(tags, qs(tag), [this, ids, tag]() {
			for (const QString &id : ids)
				controller->setEventTag(id, qs(tag));
		});
	tags->addSeparator();
	add(tags, T("Menu.TagCustom"), [this, ids]() {
		bool ok = false;
		QString tag = QInputDialog::getText(this, T("Menu.Tag"), T("Menu.TagPrompt"), QLineEdit::Normal,
						    QString(), &ok);
		if (ok)
			for (const QString &id : ids)
				controller->setEventTag(id, tag);
	});
	add(tags, T("Menu.TagClear"), [this, ids]() {
		for (const QString &id : ids)
			controller->setEventTag(id, QString());
	});
	add(&menu, event->favorite ? T("Menu.Unfavorite") : T("Menu.Favorite"), [this, ids]() {
		for (const QString &id : ids)
			controller->toggleFavorite(id);
	});
	menu.addSeparator();

	QMenu *exportMenu = menu.addMenu(T("Menu.Export"));
	const int angle = controller->preferredAngle();
	if (angle < (int)event->angles.size())
		add(exportMenu, QString(T("Menu.ExportAngle")).arg(qs(event->angles[(size_t)angle].label)),
		    [this, angle]() { exportSelected(angle, true); });
	add(exportMenu, T("Menu.ExportAll"), [this]() { exportSelected(-1, true); });
	add(exportMenu, T("Menu.ExportFull"), [this]() { exportSelected(-1, false); });

	const QString file = event->angles.empty() ? QString() : qs(event->angles.front().file);
	add(&menu, T("Menu.ShowInFolder"),
	    [file]() { QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(file).absolutePath())); });
	menu.addSeparator();
	add(&menu, T("Menu.Delete"), [this]() { deleteSelected(); });
	menu.addSeparator();
	add(&menu, T("Menu.Refresh"), [this]() { controller->refreshEvents(); });

	menu.exec(table->viewport()->mapToGlobal(pos));
}

void CueDock::renameSelected()
{
	const QString id = currentId();
	const ReplayEvent *event = controller->findEvent(id.toStdString());
	if (!event)
		return;
	bool ok = false;
	QString name = QInputDialog::getText(this, T("Rename.Title"), T("Rename.Label"), QLineEdit::Normal,
					     qs(event->name), &ok);
	if (ok && !name.trimmed().isEmpty())
		controller->renameEvent(id, name);
}

void CueDock::deleteSelected()
{
	const QStringList ids = selectedIds();
	if (ids.isEmpty())
		return;
	auto answer = QMessageBox::question(this, T("Delete.Title"), QString(T("Delete.Text")).arg(ids.size()),
					    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
	if (answer == QMessageBox::Yes)
		controller->deleteEvents(ids);
}

void CueDock::exportSelected(int angle, bool trim)
{
	for (const QString &id : selectedIds())
		controller->exportEvent(id, angle, trim);
}

void CueDock::openSettings()
{
	SettingsDialog dialog(controller->config(), this);
	if (dialog.exec() == QDialog::Accepted)
		controller->applyConfig(dialog.result());
}

void saveUiSnapshots(ReplayController *controller, const QString &folder)
{
	QDir().mkpath(folder);

	auto snap = [](QWidget *widget, const QSize &size, const QString &path) {
		widget->setAttribute(Qt::WA_DontShowOnScreen);
		widget->resize(size);
		widget->show();
		widget->grab().save(path);
		widget->hide();
	};

	CueDock dock(controller);
	snap(&dock, QSize(460, 720), QDir(folder).filePath("dock.png"));

	SettingsDialog dialog(controller->config());
	if (auto *tabs = dialog.findChild<QTabWidget *>()) {
		for (int i = 0; i < tabs->count(); i++) {
			tabs->setCurrentIndex(i);
			snap(&dialog, QSize(640, 580), QDir(folder).filePath(QString("settings-%1.png").arg(i + 1)));
		}
	}
}

} // namespace rc
