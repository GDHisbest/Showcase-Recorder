#pragma once

#include <Geode/Geode.hpp>

namespace sr {

// Перетаскиваемая кнопка поверх меню паузы.
// Короткий тап открывает SettingsPopup, перетаскивание двигает кнопку.
// Позиция сохраняется между запусками (в долях от размера экрана).
class FloatingButton : public cocos2d::CCLayer {
protected:
	cocos2d::CCPoint m_touchStart = {0.f, 0.f};
	cocos2d::CCPoint m_nodeStart = {0.f, 0.f};
	bool m_dragging = false;

	bool init() override;

	void registerWithTouchDispatcher() override;
	bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
	void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
	void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
	void ccTouchCancelled(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;

	void clampToScreen();
	void savePosition();
	void pressAnimation(bool down);

public:
	static FloatingButton* create();
};

} // namespace sr
