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

#include "settings-dialog.hpp"
#include "capture-engine.hpp"
#include "rc-common.hpp"
#include "replay-player.hpp"
#include "sport.hpp"

#include <obs-frontend-api.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cstring>

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

enum AngleColumn { ColEnabled, ColLabel, ColSource, AngleColCount };

void selectData(QComboBox *combo, const QVariant &value)
{
	int idx = combo->findData(value);
	combo->setCurrentIndex(idx >= 0 ? idx : 0);
}

QWidget *folderPicker(QLineEdit *edit, QWidget *parent)
{
	auto *row = new QWidget(parent);
	auto *layout = new QHBoxLayout(row);
	layout->setContentsMargins(0, 0, 0, 0);
	auto *browse = new QPushButton(T("Settings.Browse"), row);
	layout->addWidget(edit, 1);
	layout->addWidget(browse);
	QObject::connect(browse, &QPushButton::clicked, row, [edit, parent]() {
		QString dir = QFileDialog::getExistingDirectory(parent, T("Settings.ChooseFolder"), edit->text());
		if (!dir.isEmpty())
			edit->setText(dir);
	});
	return row;
}

struct SourceList {
	QStringList inputs;
	QStringList cameras;
	QStringList scenes;
};

SourceList listSources()
{
	SourceList list;
	obs_enum_sources(
		[](void *param, obs_source_t *source) {
			auto *out = static_cast<SourceList *>(param);
			const uint32_t flags = obs_source_get_output_flags(source);
			if (!(flags & OBS_SOURCE_VIDEO) ||
			    strcmp(obs_source_get_unversioned_id(source), kPlayerSourceId) == 0)
				return true;
			const QString name = QString::fromUtf8(obs_source_get_name(source));
			out->inputs << name;
			if (flags & OBS_SOURCE_ASYNC_VIDEO)
				out->cameras << name;
			return true;
		},
		&list);
	obs_enum_scenes(
		[](void *param, obs_source_t *source) {
			if (obs_source_is_group(source))
				return true;
			static_cast<SourceList *>(param)->scenes << QString::fromUtf8(obs_source_get_name(source));
			return true;
		},
		&list);
	list.inputs.sort(Qt::CaseInsensitive);
	list.cameras.sort(Qt::CaseInsensitive);
	return list;
}

QComboBox *transitionCombo(const std::string &current, QWidget *parent)
{
	auto *combo = new QComboBox(parent);
	combo->addItem(T("Settings.CurrentTransition"), QString());
	obs_frontend_source_list list = {};
	obs_frontend_get_transitions(&list);
	for (size_t i = 0; i < list.sources.num; i++) {
		QString name = QString::fromUtf8(obs_source_get_name(list.sources.array[i]));
		combo->addItem(name, name);
	}
	obs_frontend_source_list_free(&list);
	if (!current.empty() && combo->findData(qs(current)) < 0)
		combo->addItem(qs(current), qs(current));
	selectData(combo, qs(current));
	return combo;
}

QComboBox *speedCombo(double current, QWidget *parent)
{
	auto *combo = new QComboBox(parent);
	for (int pct : {25, 50, 75, 100})
		combo->addItem(QString("%1%").arg(pct), pct / 100.0);
	int idx = combo->findData(current);
	combo->setCurrentIndex(idx >= 0 ? idx : 3);
	return combo;
}

QString sportTags(const std::string &sportId)
{
	QStringList tags;
	for (const char *tag : findSport(sportId).tags)
		tags << QString::fromUtf8(tag);
	return tags.join(", ");
}

} // namespace

SettingsDialog::SettingsDialog(const Config &config, QWidget *parent) : QDialog(parent), initial(config)
{
	setWindowTitle(T("Settings.Title"));
	setMinimumSize(560, 480);

	auto *tabs = new QTabWidget(this);
	tabs->addTab(buildCamerasTab(), T("Settings.TabCameras"));
	tabs->addTab(buildCaptureTab(), T("Settings.TabCapture"));
	tabs->addTab(buildReplaysTab(), T("Settings.TabReplays"));
	tabs->addTab(buildPlaybackTab(), T("Settings.TabPlayback"));
	tabs->addTab(buildSportTab(), T("Settings.TabSport"));

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	auto *layout = new QVBoxLayout(this);
	layout->addWidget(tabs, 1);
	layout->addWidget(buttons);

	updateMemoryEstimate();
}

QStringList SettingsDialog::videoSourceNames() const
{
	SourceList list = listSources();
	return list.inputs + list.scenes;
}

QWidget *SettingsDialog::buildCamerasTab()
{
	auto *page = new QWidget(this);
	auto *layout = new QVBoxLayout(page);

	auto *help = new QLabel(T("Settings.CamerasHelp"), page);
	help->setWordWrap(true);
	layout->addWidget(help);

	angleTable = new QTableWidget(0, AngleColCount, page);
	angleTable->setHorizontalHeaderLabels({T("Settings.ColOn"), T("Settings.ColLabel"), T("Settings.ColSource")});
	angleTable->verticalHeader()->hide();
	angleTable->setSelectionBehavior(QAbstractItemView::SelectRows);
	angleTable->setSelectionMode(QAbstractItemView::SingleSelection);
	angleTable->horizontalHeader()->setSectionResizeMode(ColEnabled, QHeaderView::ResizeToContents);
	angleTable->horizontalHeader()->setSectionResizeMode(ColLabel, QHeaderView::Interactive);
	angleTable->horizontalHeader()->setSectionResizeMode(ColSource, QHeaderView::Stretch);
	angleTable->setColumnWidth(ColLabel, 140);
	layout->addWidget(angleTable, 1);

	for (const AngleSetting &angle : initial.angles)
		addAngleRow(angle);

	auto *buttons = new QHBoxLayout();
	auto *add = new QPushButton(T("Settings.AddCamera"), page);
	auto *addAll = new QPushButton(T("Settings.AddAllCameras"), page);
	auto *remove = new QPushButton(T("Settings.Remove"), page);
	auto *up = new QPushButton(T("Settings.MoveUp"), page);
	auto *down = new QPushButton(T("Settings.MoveDown"), page);
	buttons->addWidget(add);
	buttons->addWidget(addAll);
	buttons->addStretch(1);
	buttons->addWidget(up);
	buttons->addWidget(down);
	buttons->addWidget(remove);
	layout->addLayout(buttons);

	connect(add, &QPushButton::clicked, this, [this]() {
		if (angleTable->rowCount() >= kMaxAngles)
			return;
		AngleSetting angle;
		angle.label = "Cam " + std::to_string(angleTable->rowCount() + 1);
		addAngleRow(angle);
		updateMemoryEstimate();
	});
	connect(addAll, &QPushButton::clicked, this, &SettingsDialog::addAllCameras);
	connect(remove, &QPushButton::clicked, this, [this]() {
		int row = angleTable->currentRow();
		if (row >= 0)
			angleTable->removeRow(row);
		updateMemoryEstimate();
	});
	connect(up, &QPushButton::clicked, this, [this]() { moveSelectedRow(-1); });
	connect(down, &QPushButton::clicked, this, [this]() { moveSelectedRow(1); });
	connect(angleTable, &QTableWidget::itemChanged, this, [this]() { updateMemoryEstimate(); });
	return page;
}

void SettingsDialog::addAngleRow(const AngleSetting &angle)
{
	const int row = angleTable->rowCount();
	angleTable->insertRow(row);

	auto *enabled = new QTableWidgetItem();
	enabled->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
	enabled->setCheckState(angle.enabled ? Qt::Checked : Qt::Unchecked);
	angleTable->setItem(row, ColEnabled, enabled);
	angleTable->setItem(row, ColLabel, new QTableWidgetItem(qs(angle.label)));

	auto *combo = new QComboBox(angleTable);
	SourceList list = listSources();
	combo->addItem(T("Settings.ChooseSource"), QString());
	for (const QString &name : list.inputs)
		combo->addItem(name, name);
	if (!list.scenes.isEmpty()) {
		combo->insertSeparator(combo->count());
		for (const QString &name : list.scenes)
			combo->addItem(QString(T("Settings.ScenePrefix")).arg(name), name);
	}
	if (!angle.source.empty() && combo->findData(qs(angle.source)) < 0)
		combo->addItem(QString(T("Settings.Missing")).arg(qs(angle.source)), qs(angle.source));
	selectData(combo, qs(angle.source));
	angleTable->setCellWidget(row, ColSource, combo);
}

void SettingsDialog::addAllCameras()
{
	QStringList used;
	for (int row = 0; row < angleTable->rowCount(); row++)
		if (auto *combo = qobject_cast<QComboBox *>(angleTable->cellWidget(row, ColSource)))
			used << combo->currentData().toString();

	for (const QString &name : listSources().cameras) {
		if (angleTable->rowCount() >= kMaxAngles)
			break;
		if (used.contains(name))
			continue;
		AngleSetting angle;
		angle.label = "Cam " + std::to_string(angleTable->rowCount() + 1);
		angle.source = name.toStdString();
		addAngleRow(angle);
	}
	updateMemoryEstimate();
}

void SettingsDialog::moveSelectedRow(int delta)
{
	const int row = angleTable->currentRow();
	const int target = row + delta;
	if (row < 0 || target < 0 || target >= angleTable->rowCount())
		return;

	std::vector<AngleSetting> rows;
	for (int r = 0; r < angleTable->rowCount(); r++) {
		AngleSetting a;
		a.enabled = angleTable->item(r, ColEnabled)->checkState() == Qt::Checked;
		a.label = angleTable->item(r, ColLabel)->text().toStdString();
		if (auto *combo = qobject_cast<QComboBox *>(angleTable->cellWidget(r, ColSource)))
			a.source = combo->currentData().toString().toStdString();
		rows.push_back(a);
	}
	std::swap(rows[(size_t)row], rows[(size_t)target]);
	angleTable->setRowCount(0);
	for (const AngleSetting &a : rows)
		addAngleRow(a);
	angleTable->selectRow(target);
}

QWidget *SettingsDialog::buildCaptureTab()
{
	auto *page = new QWidget(this);
	auto *form = new QFormLayout(page);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

	armOnStartup = new QCheckBox(T("Settings.ArmOnStartup"), page);
	armOnStartup->setChecked(initial.armOnStartup);
	form->addRow(armOnStartup);

	bufferSeconds = new QSpinBox(page);
	bufferSeconds->setRange(10, 3600);
	bufferSeconds->setSuffix(" s");
	bufferSeconds->setValue(initial.bufferSeconds);
	form->addRow(T("Settings.Buffer"), bufferSeconds);

	encoder = new QComboBox(page);
	const std::string autoId = CaptureEngine::defaultVideoEncoder();
	const char *autoName = autoId.empty() ? nullptr : obs_encoder_get_display_name(autoId.c_str());
	encoder->addItem(QString(T("Settings.EncoderAuto")).arg(autoName ? QString::fromUtf8(autoName) : qs(autoId)),
			 QString());
	for (const auto &[id, name] : CaptureEngine::availableVideoEncoders())
		encoder->addItem(qs(name), qs(id));
	selectData(encoder, qs(initial.encoderId));
	form->addRow(T("Settings.Encoder"), encoder);

	bitrate = new QSpinBox(page);
	bitrate->setRange(1000, 200000);
	bitrate->setSingleStep(1000);
	bitrate->setSuffix(" kbps");
	bitrate->setValue(initial.bitrateKbps);
	form->addRow(T("Settings.Bitrate"), bitrate);

	keyint = new QSpinBox(page);
	keyint->setRange(1, 10);
	keyint->setSuffix(" s");
	keyint->setValue(initial.keyintSec);
	keyint->setToolTip(T("Settings.KeyintTip"));
	form->addRow(T("Settings.Keyint"), keyint);

	maxHeight = new QComboBox(page);
	maxHeight->addItem(T("Settings.NativeSize"), 0);
	for (int h : {2160, 1440, 1080, 720, 540})
		maxHeight->addItem(QString("%1p").arg(h), h);
	selectData(maxHeight, initial.maxHeight);
	form->addRow(T("Settings.MaxHeight"), maxHeight);

	audioTrack = new QComboBox(page);
	audioTrack->addItem(T("Settings.NoAudio"), 0);
	for (int t = 1; t <= 6; t++)
		audioTrack->addItem(QString(T("Settings.Track")).arg(t), t);
	selectData(audioTrack, initial.audioTrack);
	form->addRow(T("Settings.Audio"), audioTrack);

	captureHdr = new QCheckBox(T("Settings.CaptureHdr"), page);
	captureHdr->setChecked(initial.captureHdr);
	captureHdr->setToolTip(T("Settings.CaptureHdrTip"));
	form->addRow(captureHdr);

	memoryLabel = new QLabel(page);
	memoryLabel->setWordWrap(true);
	form->addRow(memoryLabel);

	auto *note = new QLabel(T("Settings.CaptureNote"), page);
	note->setWordWrap(true);
	form->addRow(note);

	connect(bufferSeconds, &QSpinBox::valueChanged, this, &SettingsDialog::updateMemoryEstimate);
	connect(bitrate, &QSpinBox::valueChanged, this, &SettingsDialog::updateMemoryEstimate);
	return page;
}

void SettingsDialog::updateMemoryEstimate()
{
	if (!memoryLabel || !angleTable || !bitrate || !bufferSeconds)
		return;
	int cameras = 0;
	for (int row = 0; row < angleTable->rowCount(); row++)
		if (angleTable->item(row, ColEnabled) && angleTable->item(row, ColEnabled)->checkState() == Qt::Checked)
			cameras++;
	const double mb = cameras * (double)bitrate->value() * bufferSeconds->value() / 8.0 / 1024.0;
	memoryLabel->setText(QString(T("Settings.MemoryEstimate")).arg(cameras).arg(mb, 0, 'f', 0));
}

QWidget *SettingsDialog::buildReplaysTab()
{
	auto *page = new QWidget(this);
	auto *form = new QFormLayout(page);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

	folder = new QLineEdit(qs(initial.folder), page);
	form->addRow(T("Settings.Folder"), folderPicker(folder, this));

	watchFolder = new QCheckBox(T("Settings.WatchFolder"), page);
	watchFolder->setChecked(initial.watchFolder);
	form->addRow(watchFolder);

	auto *folderHelp = new QLabel(T("Settings.FolderHelp"), page);
	folderHelp->setWordWrap(true);
	form->addRow(folderHelp);

	instantSeconds = new QSpinBox(page);
	instantSeconds->setRange(1, 600);
	instantSeconds->setSuffix(" s");
	instantSeconds->setValue(initial.instantSeconds);
	form->addRow(T("Settings.InstantLength"), instantSeconds);

	tailMs = new QSpinBox(page);
	tailMs->setRange(0, 5000);
	tailMs->setSingleStep(100);
	tailMs->setSuffix(" ms");
	tailMs->setValue(initial.tailMs);
	form->addRow(T("Settings.Tail"), tailMs);

	latencyMs = new QSpinBox(page);
	latencyMs->setRange(0, 5000);
	latencyMs->setSingleStep(50);
	latencyMs->setSuffix(" ms");
	latencyMs->setValue(initial.markLatencyMs);
	latencyMs->setToolTip(T("Settings.LatencyTip"));
	form->addRow(T("Settings.Latency"), latencyMs);

	importReplayBuffer = new QCheckBox(T("Settings.ImportReplayBuffer"), page);
	importReplayBuffer->setChecked(initial.importReplayBuffer);
	form->addRow(importReplayBuffer);

	exportFolder = new QLineEdit(qs(initial.exportFolder), page);
	form->addRow(T("Settings.ExportFolder"), folderPicker(exportFolder, this));

	QStringList tagList;
	for (const std::string &tag : initial.tags)
		tagList << qs(tag);
	tags = new QLineEdit(tagList.join(", "), page);
	form->addRow(T("Settings.Tags"), tags);
	return page;
}

QWidget *SettingsDialog::buildPlaybackTab()
{
	auto *page = new QWidget(this);
	auto *form = new QFormLayout(page);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

	sceneName = new QLineEdit(qs(initial.sceneName), page);
	form->addRow(T("Settings.SceneName"), sceneName);
	playerName = new QLineEdit(qs(initial.playerName), page);
	form->addRow(T("Settings.PlayerName"), playerName);

	replayTransition = transitionCombo(initial.replayTransition, page);
	form->addRow(T("Settings.ReplayTransition"), replayTransition);
	returnTransition = transitionCombo(initial.returnTransition, page);
	form->addRow(T("Settings.ReturnTransition"), returnTransition);

	liveScene = new QComboBox(page);
	liveScene->addItem(T("Settings.PreviousScene"), QString());
	char **names = obs_frontend_get_scene_names();
	for (char **n = names; n && *n; n++)
		liveScene->addItem(QString::fromUtf8(*n), QString::fromUtf8(*n));
	bfree(names);
	if (!initial.liveScene.empty() && liveScene->findData(qs(initial.liveScene)) < 0)
		liveScene->addItem(qs(initial.liveScene), qs(initial.liveScene));
	selectData(liveScene, qs(initial.liveScene));
	form->addRow(T("Settings.LiveScene"), liveScene);

	defaultSpeed = speedCombo(initial.defaultSpeed, page);
	form->addRow(T("Settings.DefaultSpeed"), defaultSpeed);

	autoPlayNext = new QCheckBox(T("Settings.AutoPlayNext"), page);
	autoPlayNext->setChecked(initial.autoPlayNext);
	form->addRow(autoPlayNext);
	cycleAngles = new QCheckBox(T("Settings.CycleAngles"), page);
	cycleAngles->setChecked(initial.cycleAngles);
	form->addRow(cycleAngles);
	returnToLive = new QCheckBox(T("Settings.ReturnToLive"), page);
	returnToLive->setChecked(initial.returnToLive);
	form->addRow(returnToLive);
	startCuedOnLive = new QCheckBox(T("Settings.StartCued"), page);
	startCuedOnLive->setChecked(initial.startCuedOnLive);
	form->addRow(startCuedOnLive);
	playAudio = new QCheckBox(T("Settings.PlayAudio"), page);
	playAudio->setChecked(initial.playAudio);
	form->addRow(playAudio);
	slowMotionAudio = new QComboBox(page);
	slowMotionAudio->addItem(T("Settings.SlowAudioNatural"), QStringLiteral("natural"));
	slowMotionAudio->addItem(T("Settings.SlowAudioSlowed"), QStringLiteral("slowed"));
	slowMotionAudio->addItem(T("Settings.SlowAudioOff"), QStringLiteral("off"));
	selectData(slowMotionAudio, qs(initial.slowMotionAudio));
	slowMotionAudio->setToolTip(T("Settings.SlowAudioTip"));
	form->addRow(T("Settings.SlowAudio"), slowMotionAudio);
	connect(playAudio, &QCheckBox::toggled, slowMotionAudio, &QComboBox::setEnabled);
	slowMotionAudio->setEnabled(initial.playAudio);
	hwDecode = new QCheckBox(T("Settings.HwDecode"), page);
	hwDecode->setChecked(initial.hwDecode);
	form->addRow(hwDecode);
	return page;
}

QWidget *SettingsDialog::buildSportTab()
{
	auto *page = new QWidget(this);
	auto *form = new QFormLayout(page);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

	sport = new QComboBox(page);
	for (const Sport &s : sports())
		sport->addItem(T(s.nameKey), QString::fromUtf8(s.id));
	selectData(sport, qs(initial.sport));
	form->addRow(T("Settings.Sport"), sport);

	auto *sportHelp = new QLabel(T("Settings.SportHelp"), page);
	sportHelp->setWordWrap(true);
	form->addRow(sportHelp);

	auto *useTags = new QPushButton(T("Settings.UseSportTags"), page);
	form->addRow(useTags);

	highlightNewReplays = new QCheckBox(T("Settings.HighlightNewReplays"), page);
	highlightNewReplays->setChecked(initial.highlightNewReplays);
	form->addRow(highlightNewReplays);

	highlightsScene = new QLineEdit(qs(initial.highlightsScene), page);
	highlightsScene->setPlaceholderText(T("Settings.SameAsReplayScene"));
	highlightsScene->setToolTip(T("Settings.HighlightsSceneTip"));
	form->addRow(T("Settings.HighlightsScene"), highlightsScene);

	highlightsSpeed = speedCombo(initial.highlightsSpeed, page);
	form->addRow(T("Settings.HighlightsSpeed"), highlightsSpeed);

	loopHighlights = new QCheckBox(T("Settings.LoopHighlights"), page);
	loopHighlights->setChecked(initial.loopHighlights);
	form->addRow(loopHighlights);

	auto *autoRow = new QWidget(page);
	auto *autoLayout = new QHBoxLayout(autoRow);
	autoLayout->setContentsMargins(0, 0, 0, 0);
	autoHighlights = new QCheckBox(T("Settings.AutoHighlights"), autoRow);
	autoHighlights->setChecked(initial.autoHighlights);
	autoHighlights->setToolTip(T("Settings.AutoHighlightsTip"));
	autoHighlightsDelay = new QSpinBox(autoRow);
	autoHighlightsDelay->setRange(0, 120);
	autoHighlightsDelay->setPrefix(T("Settings.AutoHighlightsAfter"));
	autoHighlightsDelay->setSuffix(" s");
	autoHighlightsDelay->setValue(initial.autoHighlightsDelay);
	autoHighlightsDelay->setEnabled(initial.autoHighlights);
	autoLayout->addWidget(autoHighlights);
	autoLayout->addWidget(autoHighlightsDelay);
	autoLayout->addStretch(1);
	form->addRow(autoRow);
	connect(autoHighlights, &QCheckBox::toggled, autoHighlightsDelay, &QSpinBox::setEnabled);

	auto *highlightsHelp = new QLabel(T("Settings.HighlightsHelp"), page);
	highlightsHelp->setWordWrap(true);
	form->addRow(highlightsHelp);

	connect(useTags, &QPushButton::clicked, this,
		[this]() { tags->setText(sportTags(sport->currentData().toString().toStdString())); });
	/* switching sport swaps the quick tags too, unless they were edited */
	connect(sport, &QComboBox::currentIndexChanged, this, [this, previous = initial.sport]() mutable {
		const std::string next = sport->currentData().toString().toStdString();
		if (tags->text().trimmed() == sportTags(previous))
			tags->setText(sportTags(next));
		previous = next;
	});
	return page;
}

Config SettingsDialog::result() const
{
	Config cfg = initial;

	cfg.angles.clear();
	for (int row = 0; row < angleTable->rowCount() && row < kMaxAngles; row++) {
		AngleSetting a;
		a.enabled = angleTable->item(row, ColEnabled)->checkState() == Qt::Checked;
		a.label = angleTable->item(row, ColLabel)->text().trimmed().toStdString();
		if (a.label.empty())
			a.label = "Cam " + std::to_string(row + 1);
		if (auto *combo = qobject_cast<QComboBox *>(angleTable->cellWidget(row, ColSource)))
			a.source = combo->currentData().toString().toStdString();
		cfg.angles.push_back(a);
	}

	cfg.armOnStartup = armOnStartup->isChecked();
	cfg.bufferSeconds = bufferSeconds->value();
	cfg.encoderId = encoder->currentData().toString().toStdString();
	cfg.bitrateKbps = bitrate->value();
	cfg.keyintSec = keyint->value();
	cfg.maxHeight = maxHeight->currentData().toInt();
	cfg.audioTrack = audioTrack->currentData().toInt();
	cfg.captureHdr = captureHdr->isChecked();

	cfg.folder = folder->text().trimmed().toStdString();
	if (cfg.folder.empty())
		cfg.folder = Config::defaultFolder();
	cfg.watchFolder = watchFolder->isChecked();
	cfg.instantSeconds = instantSeconds->value();
	cfg.tailMs = tailMs->value();
	cfg.markLatencyMs = latencyMs->value();
	cfg.importReplayBuffer = importReplayBuffer->isChecked();
	cfg.exportFolder = exportFolder->text().trimmed().toStdString();
	cfg.tags.clear();
	for (const QString &tag : tags->text().split(',', Qt::SkipEmptyParts))
		if (!tag.trimmed().isEmpty())
			cfg.tags.push_back(tag.trimmed().toStdString());

	cfg.sceneName = sceneName->text().trimmed().toStdString();
	if (cfg.sceneName.empty())
		cfg.sceneName = "Replay";
	cfg.playerName = playerName->text().trimmed().toStdString();
	if (cfg.playerName.empty())
		cfg.playerName = "Replay Cue Player";
	cfg.replayTransition = replayTransition->currentData().toString().toStdString();
	cfg.returnTransition = returnTransition->currentData().toString().toStdString();
	cfg.liveScene = liveScene->currentData().toString().toStdString();
	cfg.defaultSpeed = defaultSpeed->currentData().toDouble();
	cfg.autoPlayNext = autoPlayNext->isChecked();
	cfg.cycleAngles = cycleAngles->isChecked();
	cfg.returnToLive = returnToLive->isChecked();
	cfg.startCuedOnLive = startCuedOnLive->isChecked();
	cfg.playAudio = playAudio->isChecked();
	cfg.slowMotionAudio = slowMotionAudio->currentData().toString().toStdString();
	cfg.hwDecode = hwDecode->isChecked();

	cfg.sport = sport->currentData().toString().toStdString();
	cfg.highlightNewReplays = highlightNewReplays->isChecked();
	cfg.highlightsScene = highlightsScene->text().trimmed().toStdString();
	cfg.highlightsSpeed = highlightsSpeed->currentData().toDouble();
	cfg.loopHighlights = loopHighlights->isChecked();
	cfg.autoHighlights = autoHighlights->isChecked();
	cfg.autoHighlightsDelay = autoHighlightsDelay->value();
	return cfg;
}

} // namespace rc
