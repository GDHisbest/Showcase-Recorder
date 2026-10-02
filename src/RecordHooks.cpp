#include <Geode/Geode.hpp>
#include <Geode/modify/CCScheduler.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include "Recorder.hpp"

using namespace geode::prelude;

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
	// Вызывается один раз за тик, пока уровень не на паузе
	void update(float dt) {
		PlayLayer::update(dt);
		sr::Recorder::get().markLevelUpdated();
	}

	// После прохождения пишем ещё 3 секунды видео, чтобы поймать эффекты
	void levelComplete() {
		PlayLayer::levelComplete();
		sr::Recorder::get().stopAfter(3.f);
	}

	// Выход из уровня закрывает файл
	void onQuit() {
		sr::Recorder::get().stop();
		PlayLayer::onQuit();
	}
};
