#pragma once

#include <Geode/Geode.hpp>
#include <functional>
#include <string>

namespace sr {

// Окно с настройками рендера. Открывается по тапу на FloatingButton.
// Все изменения сразу пишутся в RenderSettings и сохраняются.
class SettingsPopup : public geode::Popup {
protected:
	inline static bool s_open = false;

	bool init();

	// Строка: название слева, [<] значение [>] справа
	void addStepperRow(
		char const* title, float y,
		std::function<std::string()> getText,
		std::function<void(int)> step
	);

	// Строка: название слева, поле ввода текста справа
	void addTextRow(
		char const* title, float y, std::string const& initial,
		std::function<void(std::string const&)> onChange
	);

	// Строка: название слева, переключатель справа
	void addToggleRow(char const* title, float y, bool initial, std::function<void(bool)> onChange);

public:
	~SettingsPopup() override { s_open = false; }

	static SettingsPopup* create();

	// true, пока окно открыто (кнопка в этот момент не реагирует на касания)
	static bool isOpen() { return s_open; }
};

} // namespace sr
