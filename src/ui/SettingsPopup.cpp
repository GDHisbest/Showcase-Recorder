#include "SettingsPopup.hpp"
#include "../RenderSettings.hpp"
#include "../Recorder.hpp"

#include <Geode/ui/TextInput.hpp>

#include <algorithm>
#include <utility>
#include <vector>

using namespace geode::prelude;

namespace {

// Наборы значений для переключения стрелками
const std::vector<int> kFps{30, 60, 90, 120, 144, 240};
const std::vector<int> kAudioBitrates{96, 128, 192, 256, 320};

// Пока заглушки: позже заменим реальным списком кодеков из FFmpeg API
const std::vector<std::string> kVideoCodecs{"libx264", "h264_mediacodec", "libx265", "mpeg4"};
const std::vector<std::string> kAudioCodecs{"aac", "libmp3lame", "libopus"};

// Переключает значение по кругу на dir (-1 или +1)
template <class T>
void cycle(T& value, std::vector<T> const& list, int dir) {
	int n = static_cast<int>(list.size());
	int i = 0;
	auto it = std::find(list.begin(), list.end(), value);
	if (it != list.end()) i = static_cast<int>(it - list.begin());
	i = ((i + dir) % n + n) % n;
	value = list[i];
}

// Применяет ограничения и сохраняет настройки
void commit() {
	auto& s = sr::RenderSettings::current();
	s.sanitize();
	s.save();
}

} // namespace

namespace sr {

SettingsPopup* SettingsPopup::create() {
	auto ret = new SettingsPopup();
	if (ret->init()) {
		ret->autorelease();
		return ret;
	}
	CC_SAFE_DELETE(ret);
	return nullptr;
}

bool SettingsPopup::init() {
	if (!Popup::init(340.f, 360.f)) return false;
	s_open = true;

	this->setTitle("Render Settings");

	auto* s = &RenderSettings::current();

	float y = 124.f;
	constexpr float dy = 24.f;

	this->addStepperRow("Video FPS", y,
		[s] { return fmt::format("{}", s->fps); },
		[s](int d) { cycle(s->fps, kFps, d); commit(); });
	y -= dy;

	this->addStepperRow("Video bitrate", y,
		[s] { return fmt::format("{} Mbps", s->videoBitrateKbps / 1000); },
		[s](int d) {
			s->videoBitrateKbps = std::max(1000, s->videoBitrateKbps + d * 1000);
			commit();
		});
	y -= dy;

	this->addStepperRow("Video codec", y,
		[s] { return s->videoCodec; },
		[s](int d) { cycle(s->videoCodec, kVideoCodecs, d); commit(); });
	y -= dy;

	this->addStepperRow("Audio codec", y,
		[s] { return s->audioCodec; },
		[s](int d) { cycle(s->audioCodec, kAudioCodecs, d); commit(); });
	y -= dy;

	this->addStepperRow("Audio bitrate", y,
		[s] { return fmt::format("{} kbps", s->audioBitrateKbps); },
		[s](int d) { cycle(s->audioBitrateKbps, kAudioBitrates, d); commit(); });
	y -= dy;

	this->addToggleRow("No audio", y, s->noAudio,
		[s](bool on) { s->noAudio = on; commit(); });
	y -= dy;

	this->addStepperRow("Fade in", y,
		[s] { return s->fadeIn <= 0.f ? std::string("off") : fmt::format("{:.1f}s", s->fadeIn); },
		[s](int d) { s->fadeIn = std::clamp(s->fadeIn + d * 0.5f, 0.f, 30.f); commit(); });
	y -= dy;

	this->addStepperRow("Fade out", y,
		[s] { return s->fadeOut <= 0.f ? std::string("off") : fmt::format("{:.1f}s", s->fadeOut); },
		[s](int d) { s->fadeOut = std::clamp(s->fadeOut + d * 0.5f, 0.f, 30.f); commit(); });

	// Строки с дополнительными аргументами для видео и аудио
	y -= 36.f;
	this->addTextRow("Video args", y, s->videoArgs,
		[s](std::string const& text) { s->videoArgs = text; commit(); });
	y -= 32.f;

	this->addTextRow("Audio args", y, s->audioArgs,
		[s](std::string const& text) { s->audioArgs = text; commit(); });

	// Кнопка запуска записи: стартуем, закрываем окно и перезапускаем уровень,
	// потому что звук берётся с начала трека
	auto recSprite = ButtonSprite::create("Start recording");
	recSprite->setScale(0.8f);
	auto recBtn = CCMenuItemExt::createSpriteExtra(recSprite, [this](auto*) {
		if (!Recorder::get().start()) {
			Notification::create("Could not start recording", NotificationIcon::Error)->show();
			return;
		}
		Notification::create("Recording started", NotificationIcon::Success)->show();
		this->keyBackClicked();

		// Перезапуск уровня из паузы: запись и музыка начинаются с начала
		if (auto pause = CCScene::get()->getChildByType<PauseLayer>(0)) {
			pause->onRestart(nullptr);
		}
	});
	m_buttonMenu->addChildAtPosition(recBtn, Anchor::Center, {0.f, -146.f});

	return true;
}

void SettingsPopup::addStepperRow(
	char const* title, float y,
	std::function<std::string()> getText,
	std::function<void(int)> step
) {
	float half = m_size.width / 2.f;

	auto name = CCLabelBMFont::create(title, "bigFont.fnt");
	name->setScale(0.4f);
	name->setAnchorPoint({0.f, 0.5f});
	m_mainLayer->addChildAtPosition(name, Anchor::Center, {-half + 16.f, y});

	// Значение в центре правой группы, цвет неоновый циан
	auto value = CCLabelBMFont::create(getText().c_str(), "bigFont.fnt");
	value->setScale(0.4f);
	value->setColor(ccc3(0, 255, 240));
	m_mainLayer->addChildAtPosition(value, Anchor::Center, {66.f, y});

	auto makeArrow = [](bool pointRight) {
		auto spr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
		spr->setScale(0.45f);
		spr->setFlipX(pointRight);
		return spr;
	};

	auto left = CCMenuItemExt::createSpriteExtra(makeArrow(false), [=](auto*) {
		step(-1);
		value->setString(getText().c_str());
	});
	auto right = CCMenuItemExt::createSpriteExtra(makeArrow(true), [=](auto*) {
		step(1);
		value->setString(getText().c_str());
	});

	m_buttonMenu->addChildAtPosition(left, Anchor::Center, {8.f, y});
	m_buttonMenu->addChildAtPosition(right, Anchor::Center, {124.f, y});
}

void SettingsPopup::addTextRow(
	char const* title, float y, std::string const& initial,
	std::function<void(std::string const&)> onChange
) {
	float half = m_size.width / 2.f;

	auto name = CCLabelBMFont::create(title, "bigFont.fnt");
	name->setScale(0.4f);
	name->setAnchorPoint({0.f, 0.5f});
	m_mainLayer->addChildAtPosition(name, Anchor::Center, {-half + 16.f, y});

	auto input = TextInput::create(150.f, "extra args", "chatFont.fnt");
	input->setMaxCharCount(300);
	input->setString(initial, false);
	input->setCallback([onChange](std::string const& text) { onChange(text); });
	m_mainLayer->addChildAtPosition(input, Anchor::Center, {66.f, y});
}

void SettingsPopup::addToggleRow(
	char const* title, float y, bool initial, std::function<void(bool)> onChange
) {
	float half = m_size.width / 2.f;

	auto name = CCLabelBMFont::create(title, "bigFont.fnt");
	name->setScale(0.4f);
	name->setAnchorPoint({0.f, 0.5f});
	m_mainLayer->addChildAtPosition(name, Anchor::Center, {-half + 16.f, y});

	auto toggler = CCMenuItemExt::createTogglerWithStandardSprites(0.5f, [onChange](CCMenuItemToggler* t) {
		// Колбэк вызывается до переключения, поэтому новое состояние = !isToggled()
		onChange(!t->isToggled());
	});
	toggler->toggle(initial);
	m_buttonMenu->addChildAtPosition(toggler, Anchor::Center, {66.f, y});
}

} // namespace sr
