#include <Geode/Geode.hpp>
#include <Geode/modify/CCScheduler.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include "Recorder.hpp"

using namespace geode::prelude;

// Во время записи каждый шаг игры равен ровно одному кадру видео (1 / fps).
// Поэтому FPS видео не зависит ни от FPS экрана, ни от TPS физики.
class $modify(SRScheduler, CCScheduler) {
	void update(float dt) {
		auto& rec = sr::Recorder::get();
		if (rec.isRecording()) dt = rec.stepDt();
		CCScheduler::update(dt);
	}
};

class $modify(SRPlayLayer, PlayLayer) {
	// Вызывается после каждого шага игры (на паузе не вызывается)
	void postUpdate(float dt) {
		PlayLayer::postUpdate(dt);

		auto& rec = sr::Recorder::get();
		if (rec.isRecording()) rec.captureFrame(this);
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
