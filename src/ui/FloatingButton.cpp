#include "FloatingButton.hpp"
#include "SettingsPopup.hpp"
#include "../Recorder.hpp"

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace sr {

namespace {
	const ccColor3B kMagenta = {255, 70, 200};

	// Порог в пикселях: меньше - это тап, больше - перетаскивание
	constexpr float kDragThreshold = 10.f;
	constexpr float kEdgeMargin = 6.f;
}

FloatingButton* FloatingButton::create() {
	auto ret = new FloatingButton();
	if (ret->init()) {
		ret->autorelease();
		return ret;
	}
	delete ret;
	return nullptr;
}

bool FloatingButton::init() {
	if (!CCLayer::init()) return false;

	auto sprite = CCSprite::createWithSpriteFrameName("GJ_button_01.png");
	sprite->setScale(0.6f);
	auto size = sprite->getScaledContentSize();

	this->setContentSize(size);
	this->ignoreAnchorPointForPosition(false);
	this->setAnchorPoint({0.5f, 0.5f});

	sprite->setPosition({size.width / 2, size.height / 2});
	this->addChild(sprite);

	m_label = CCLabelBMFont::create(Recorder::get().isRecording() ? "STOP" : "REC", "bigFont.fnt");
	m_label->setScale(0.4f);
	m_label->setColor(kMagenta);
	m_label->setPosition({size.width / 2, size.height / 2});
	this->addChild(m_label);

	// Один касающийся палец, обработка через registerWithTouchDispatcher
	this->setTouchMode(kCCTouchesOneByOne);
	this->setTouchEnabled(true);

	// Позиция хранится в долях экрана, чтобы не ломаться при смене разрешения
	auto* mod = Mod::get();
	auto win = CCDirector::get()->getWinSize();
	float nx = mod->getSavedValue<float>("button-x", 0.94f);
	float ny = mod->getSavedValue<float>("button-y", 0.5f);
	this->setPosition({win.width * nx, win.height * ny});
	this->clampToScreen();

	return true;
}

void FloatingButton::registerWithTouchDispatcher() {
	// Приоритет должен быть ниже (то есть раньше), чем у PauseLayer,
	// иначе пауза заберёт касание. Если кнопка не реагирует - подбери значение.
	CCDirector::get()->getTouchDispatcher()->addTargetedDelegate(this, -600, true);
}

bool FloatingButton::ccTouchBegan(CCTouch* touch, CCEvent*) {
	if (!this->isVisible()) return false;

	// Пока открыт попап, кнопка не должна перехватывать касания под ним
	if (SettingsPopup::isOpen()) return false;

	auto local = this->convertToNodeSpace(touch->getLocation());
	auto size = this->getContentSize();
	if (local.x < 0 || local.y < 0 || local.x > size.width || local.y > size.height) {
		return false;
	}

	m_touchStart = touch->getLocation();
	m_nodeStart = this->getPosition();
	m_dragging = false;
	this->pressAnimation(true);
	return true;
}

void FloatingButton::ccTouchMoved(CCTouch* touch, CCEvent*) {
	auto delta = touch->getLocation() - m_touchStart;

	if (!m_dragging && std::hypot(delta.x, delta.y) > kDragThreshold) {
		m_dragging = true;
	}
	if (m_dragging) {
		this->setPosition(m_nodeStart + delta);
		this->clampToScreen();
	}
}

void FloatingButton::ccTouchEnded(CCTouch*, CCEvent*) {
	this->pressAnimation(false);

	if (m_dragging) {
		m_dragging = false;
		this->savePosition();
		return;
	}

	// Во время записи тап по кнопке останавливает запись
	if (Recorder::get().isRecording()) {
		Recorder::get().stop();
		if (m_label) m_label->setString("REC");
		return;
	}

	if (auto popup = SettingsPopup::create()) {
		popup->show();
	}
}

void FloatingButton::ccTouchCancelled(CCTouch*, CCEvent*) {
	this->pressAnimation(false);
	if (m_dragging) {
		m_dragging = false;
		this->savePosition();
	}
}

void FloatingButton::clampToScreen() {
	auto win = CCDirector::get()->getWinSize();
	auto half = this->getScaledContentSize() / 2;
	auto pos = this->getPosition();

	pos.x = std::clamp(pos.x, half.width + kEdgeMargin, win.width - half.width - kEdgeMargin);
	pos.y = std::clamp(pos.y, half.height + kEdgeMargin, win.height - half.height - kEdgeMargin);
	this->setPosition(pos);
}

void FloatingButton::savePosition() {
	auto win = CCDirector::get()->getWinSize();
	auto pos = this->getPosition();
	auto* mod = Mod::get();
	mod->setSavedValue("button-x", pos.x / win.width);
	mod->setSavedValue("button-y", pos.y / win.height);
}

void FloatingButton::pressAnimation(bool down) {
	this->stopAllActions();
	if (down) {
		this->runAction(CCScaleTo::create(0.08f, 0.9f));
	} else {
		this->runAction(CCEaseBackOut::create(CCScaleTo::create(0.2f, 1.f)));
	}
}

} // namespace sr
