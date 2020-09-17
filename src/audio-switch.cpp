#include "headers/advanced-scene-switcher.hpp"

#include "headers/volume-control.hpp"

void SceneSwitcher::on_audioSwitches_currentRowChanged(int idx)
{
	if (loading)
		return;
	if (idx == -1)
		return;

	QListWidgetItem *item = ui->audioSwitches->item(idx);

	QString audioScenestr = item->data(Qt::UserRole).toString();

	std::lock_guard<std::mutex> lock(switcher->m);
	for (auto &s : switcher->audioSwitches) {
		if (audioScenestr.compare(s.audioSwitchStr.c_str()) == 0) {
			QString sceneName = GetWeakSourceName(s.scene).c_str();
			QString transitionName =
				GetWeakSourceName(s.transition).c_str();
			ui->audioScenes->setCurrentText(sceneName);
			ui->audioTransitions->setCurrentText(transitionName);
			break;
		}
	}
}

int SceneSwitcher::audioFindByData(const QString &source, const double &volume)
{
	QRegExp rx(MakeAudioSwitchName(QStringLiteral(".*"),
				       QStringLiteral(".*")));
	int count = ui->audioSwitches->count();

	for (int i = 0; i < count; i++) {
		QListWidgetItem *item = ui->audioSwitches->item(i);
		QString str = item->data(Qt::UserRole).toString();

		if (rx.exactMatch(str))
			return i;
	}

	return -1;
}

void SceneSwitcher::on_audioAdd_clicked()
{
	QString sceneName = ui->audioScenes->currentText();
	QString transitionName = ui->audioTransitions->currentText();
	QString audioSourceName = ui->audioSources->currentText();
	double vol = ui->audioVolumeThreshold->value();

	if (sceneName.isEmpty() || audioSourceName.isEmpty())
		return;

	OBSWeakSource source = GetWeakSourceByQString(sceneName);
	OBSWeakSource audioSource = GetWeakSourceByQString(audioSourceName);
	OBSWeakSource transition = GetWeakTransitionByQString(transitionName);

	QString text = MakeAudioSwitchName(sceneName, transitionName);
	QVariant v = QVariant::fromValue(text);

	int idx = audioFindByData(audioSourceName, vol);

	if (idx == -1) {
		std::lock_guard<std::mutex> lock(switcher->m);
		switcher->audioSwitches.emplace_back(
			source, transition,
			(sceneName == QString(previous_scene_name)),
			text.toUtf8().constData());

		QListWidgetItem *item =
			new QListWidgetItem(text, ui->audioSwitches);
		item->setData(Qt::UserRole, v);
	} else {
		QListWidgetItem *item = ui->audioSwitches->item(idx);
		item->setText(text);
		item->setData(Qt::UserRole, v);

		{
			std::lock_guard<std::mutex> lock(switcher->m);
			for (auto &s : switcher->audioSwitches) {
				if (s.audioSource == audioSource &&
				    s.volume == vol) {
					s.scene = source;
					s.transition = transition;
					s.usePreviousScene =
						(sceneName ==
						 QString(previous_scene_name));
					s.audioSwitchStr =
						text.toUtf8().constData();
					break;
				}
			}
		}
	}
}

void SceneSwitcher::on_audioRemove_clicked()
{
	QListWidgetItem *item = ui->audioSwitches->currentItem();
	if (!item)
		return;

	std::string text =
		item->data(Qt::UserRole).toString().toUtf8().constData();

	{
		std::lock_guard<std::mutex> lock(switcher->m);
		auto &switches = switcher->audioSwitches;

		for (auto it = switches.begin(); it != switches.end(); ++it) {
			auto &s = *it;

			if (s.audioSwitchStr == text) {
				switches.erase(it);
				break;
			}
		}
	}

	delete item;
}

void SceneSwitcher::on_audioUp_clicked()
{
	int index = ui->audioSwitches->currentRow();
	if (index != -1 && index != 0) {
		ui->audioSwitches->insertItem(
			index - 1, ui->audioSwitches->takeItem(index));
		ui->audioSwitches->setCurrentRow(index - 1);

		std::lock_guard<std::mutex> lock(switcher->m);

		iter_swap(switcher->audioSwitches.begin() + index,
			  switcher->audioSwitches.begin() + index - 1);
	}
}

void SceneSwitcher::on_audioDown_clicked()
{
	int index = ui->audioSwitches->currentRow();
	if (index != -1 && index != ui->audioSwitches->count() - 1) {
		ui->audioSwitches->insertItem(
			index + 1, ui->audioSwitches->takeItem(index));
		ui->audioSwitches->setCurrentRow(index + 1);

		std::lock_guard<std::mutex> lock(switcher->m);

		iter_swap(switcher->audioSwitches.begin() + index,
			  switcher->audioSwitches.begin() + index + 1);
	}
}

void SwitcherData::checkAudioSwitch(bool &match, OBSWeakSource &scene,
				    OBSWeakSource &transition)
{
	if (audioSwitches.size() == 0)
		return;

	for (AudioSwitch &s : audioSwitches) {
		//TODO

		if (match) {
			scene = (s.usePreviousScene) ? previousScene : s.scene;
			transition = s.transition;
			match = true;

			if (verbose)
				blog(LOG_INFO,
				     "Advanced Scene Switcher audio match");

			break;
		}
	}
}

void SwitcherData::saveAudioSwitches(obs_data_t *obj)
{
	obs_data_array_t *audioArray = obs_data_array_create();
	for (AudioSwitch &s : switcher->audioSwitches) {
		obs_data_t *array_obj = obs_data_create();

		obs_source_t *sceneSource = obs_weak_source_get_source(s.scene);
		obs_source_t *transition =
			obs_weak_source_get_source(s.transition);
		if ((s.usePreviousScene || sceneSource) && transition) {
			const char *sceneName =
				obs_source_get_name(sceneSource);
			const char *transitionName =
				obs_source_get_name(transition);
			obs_data_set_string(array_obj, "scene",
					    s.usePreviousScene
						    ? previous_scene_name
						    : sceneName);
			obs_data_set_string(array_obj, "transition",
					    transitionName);
			obs_data_array_push_back(audioArray, array_obj);
		}
		obs_source_release(sceneSource);
		obs_source_release(transition);

		obs_data_release(array_obj);
	}
	obs_data_set_array(obj, "audioSwitches", audioArray);
	obs_data_array_release(audioArray);
}

void SwitcherData::loadAudioSwitches(obs_data_t *obj)
{
	switcher->audioSwitches.clear();

	obs_data_array_t *audioArray = obs_data_get_array(obj, "audioSwitches");
	size_t count = obs_data_array_count(audioArray);

	for (size_t i = 0; i < count; i++) {
		obs_data_t *array_obj = obs_data_array_item(audioArray, i);

		const char *scene = obs_data_get_string(array_obj, "scene");
		const char *transition =
			obs_data_get_string(array_obj, "transition");

		std::string audioSwitchStr =
			MakeAudioSwitchName(scene, transition)
				.toUtf8()
				.constData();

		switcher->audioSwitches.emplace_back(
			GetWeakSourceByName(scene),
			GetWeakTransitionByName(transition),
			(strcmp(scene, previous_scene_name) == 0),
			audioSwitchStr);

		obs_data_release(array_obj);
	}
	obs_data_array_release(audioArray);
}

void SceneSwitcher::setupAudioTab()
{
	populateSceneSelection(ui->audioScenes, true);
	populateTransitionSelection(ui->audioTransitions);

	auto sourceEnum = [](void *data, obs_source_t *source) -> bool /* -- */
	{
		QComboBox *combo = reinterpret_cast<QComboBox *>(data);
		uint32_t flags = obs_source_get_output_flags(source);

		if ((flags & OBS_SOURCE_AUDIO) != 0) {
			const char *name = obs_source_get_name(source);
			combo->addItem(name);
		}
		return true;
	};

	obs_enum_sources(sourceEnum, ui->audioSources);

	//just for testing
	obs_source_t *test = obs_get_source_by_name("Media Source 3");
	VolControl *vol = new VolControl(test);
	ui->audioControlLayout->addWidget(vol);
	obs_source_release(test);

	for (auto &s : switcher->audioSwitches) {
		std::string sceneName = (s.usePreviousScene)
						? previous_scene_name
						: GetWeakSourceName(s.scene);
		std::string transitionName = GetWeakSourceName(s.transition);
		QString listText = MakeAudioSwitchName(sceneName.c_str(),
						       transitionName.c_str());

		QListWidgetItem *item =
			new QListWidgetItem(listText, ui->audioSwitches);
		item->setData(Qt::UserRole, listText);
	}
}
