#include <Geode/Geode.hpp>
#include <Geode/modify/CCScheduler.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include <Geode/modify/EndLevelLayer.hpp>

#include "Recorder.hpp"
#include "RenderSettings.hpp"

using namespace geode::prelude;

// No endscreen: во время записи экран завершения уровня скрывается, поэтому его
// нет в видео (кадры рисуются из PlayLayer вместе с его дочерними узлами)
class $modify(SREndLevelLayer, EndLevelLayer) {
	void customSetup() {
		EndLevelLayer::customSetup();
		if (sr::Recorder::get().isRecording() && sr::RenderSettings::current().noEndscreen) {
			this->setVisible(false);
		}
	}
};

// Во время записи каждый тик планировщика равен ровно 1 / fps секунд игрового
// времени, а в конце тика пишется ровно один кадр. Поэтому скорость видео
// нормальная и не зависит ни от FPS экрана, ни от TPS физики, ни от того,
// насколько медленно телефон рисует кадры во время записи.
class $modify(SRScheduler, CCScheduler) {
	void update(float dt) {
		auto& rec = sr::Recorder::get();
		if (!rec.isRecording()) {
			CCScheduler::update(dt);
			return;
		}

		CCScheduler::update(rec.stepDt());
		rec.endTick();
	}
};

class $modify(SRPlayLayer, PlayLayer) {
	// Вызывается, пока уровень идёт (на паузе не вызывается). За тик может
	// вызваться несколько раз, поэтому только ставим флаг, а кадр пишет endTick.
	void postUpdate(float dt) {
		PlayLayer::postUpdate(dt);
		sr::Recorder::get().markLevelUpdated();
	}

	// После прохождения пишем ещё заданное время видео, чтобы поймать эффекты
	void levelComplete() {
		PlayLayer::levelComplete();
		sr::Recorder::get().stopAfter(sr::RenderSettings::current().timeAfterComplete);
	}

	// Выход из уровня закрывает файл
	void onQuit() {
		sr::Recorder::get().stop();
		PlayLayer::onQuit();
	}
};
