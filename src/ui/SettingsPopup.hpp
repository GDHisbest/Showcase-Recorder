#pragma once

#include <Geode/Geode.hpp>
#include <functional>
#include <string>

namespace sr {

// Окно с настройками рендера (две колонки: видео и аудио, помещается на экран).
// Открывается по тапу на FloatingButton.
// Все изменения сразу пишутся в RenderSettings и сохраняются.
class SettingsPopup : public geode::Popup {
protected:
	inline static bool s_open = false;

	bool init();

	// Заголовок колонки (cx, y - смещение от центра окна)
	void addHeader(char const* text, float cx, float y, cocos2d::ccColor3B color);

	// Ряд кнопок выбора разрешения 720p ... 8K (используется на ПК)
	void addResolutionRow(float y);

	// Подпись сверху, под ней [<] значение [>]
	void addStepperRow(
		char const* title, float cx, float y,
		std::function<std::string()> getText,
		std::function<void(int)> step
	);

	// Подпись сверху, под ней поле ввода текста
	void addTextRow(
		char const* title, float cx, float y, std::string const& initial,
		std::function<void(std::string const&)> onChange
	);

	// Подпись сверху, под ней переключатель
	void addToggleRow(
		char const* title, float cx, float y, bool initial,
		std::function<void(bool)> onChange
	);

public:
	~SettingsPopup() override { s_open = false; }

	static SettingsPopup* create();

	// true, пока окно открыто (кнопка в этот момент не реагирует на касания)
	static bool isOpen() { return s_open; }
};

} // namespace sr
