#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>

#include "ui/FloatingButton.hpp"

using namespace geode::prelude;

class $modify(SRPauseLayer, PauseLayer) {
	void customSetup() {
		PauseLayer::customSetup();

		auto btn = sr::FloatingButton::create();
		if (!btn) return;

		btn->setID("floating-button"_spr);
		// Высокий z-order, чтобы кнопка была поверх меню паузы
		this->addChild(btn, 100);
	}
};
