#include "SettingsPopup.hpp"
#include "../RenderSettings.hpp"
#include "../Recorder.hpp"

#include <Geode/ui/TextInput.hpp>

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

using namespace geode::prelude;

namespace {

// Неоновая палитра окна
const ccColor3B kCyan = {0, 255, 240};
const ccColor3B kMagenta = {255, 70, 200};
const ccColor3B kLabel = {180, 190, 255};

// Наборы значений для переключения стрелками
const std::vector<int> kFps{30, 60, 90, 120, 144, 240};
const std::vector<int> kAudioBitrates{96, 128, 192, 256, 320};

#ifdef GEODE_IS_DESKTOP
// ПК: программные и аппаратные кодеки (NVIDIA, AMD, Intel). Какие из них реально
// доступны, зависит от видеокарты и сборки FFmpeg в FFmpeg API.
const std::vector<std::string> kVideoCodecs{
	"libx264", "h264_nvenc", "hevc_nvenc", "av1_nvenc",
	"h264_amf", "hevc_amf", "h264_qsv", "hevc_qsv",
	"libx265", "libvpx-vp9", "libsvtav1", "mpeg4"
};
const std::vector<std::string> kAudioCodecs{"aac", "libmp3lame", "libopus"};
#else
// Android: программные кодеки и аппаратные через MediaCodec
const std::vector<std::string> kVideoCodecs{"libx264", "h264_mediacodec", "libx265", "mpeg4"};
const std::vector<std::string> kAudioCodecs{"aac", "libmp3lame", "libopus"};
#endif

// Разрешения для ПК: название кнопки и высота кадра (ширина 16:9)
const std::vector<std::pair<char const*, int>> kResolutions{
	{"720p", 720}, {"1080p", 1080}, {"1440p", 1440}, {"4K", 2160}, {"8K", 4320}
};

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
	// Высота экрана GD всего 320 единиц, поэтому окно компактное: две колонки.
	// На ПК сверху добавляется ряд кнопок разрешения, окно выше на 31.
#ifdef GEODE_IS_DESKTOP
	constexpr float extra = 31.f;
#else
	constexpr float extra = 0.f;
#endif
	constexpr float H = 270.f + extra;
	// Y по расстоянию от верхнего края окна (d): удобно, когда высота зависит от платформы
	auto Y = [](float d) { return H / 2.f - d; };

	if (!Popup::init(400.f, H)) return false;
	s_open = true;

	this->setTitle("Showcase Recorder");
	if (m_bgSprite) m_bgSprite->setColor({16, 12, 40}); // тёмно-синий фон

	auto* s = &RenderSettings::current();

	constexpr float L = -100.f; // центр левой колонки (видео)
	constexpr float R = 100.f;  // центр правой колонки (аудио)

	// Тонкая неоновая линия между колонками
	auto divider = CCLayerColor::create(ccc4(0, 255, 240, 70), 1.f, 165.f);
	divider->ignoreAnchorPointForPosition(false);
	m_mainLayer->addChildAtPosition(divider, Anchor::Center, {0.f, Y(139.f + extra)});

#ifdef GEODE_IS_DESKTOP
	this->addResolutionRow(Y(41.f)); // между заголовком окна и VIDEO/AUDIO
#endif

	this->addHeader("VIDEO", L, Y(35.f + extra), kCyan);
	this->addHeader("AUDIO", R, Y(35.f + extra), kMagenta);

	// Левая колонка: видео
	this->addStepperRow("FPS", L, Y(59.f + extra),
		[s] { return fmt::format("{}", s->fps); },
		[s](int d) { cycle(s->fps, kFps, d); commit(); });

	this->addStepperRow("Bitrate", L, Y(90.f + extra),
		[s] { return fmt::format("{} Mbps", s->videoBitrateKbps / 1000); },
		[s](int d) {
			// Шаг крупнее на больших значениях (для 4K и 8K)
			int step = s->videoBitrateKbps < 20000 ? 1000 : 5000;
			if (d < 0 && s->videoBitrateKbps <= 20000) step = 1000;
			s->videoBitrateKbps = std::max(1000, s->videoBitrateKbps + d * step);
			commit();
		});

	this->addStepperRow("Codec", L, Y(121.f + extra),
		[s] { return s->videoCodec; },
		[s](int d) { cycle(s->videoCodec, kVideoCodecs, d); commit(); });

	this->addTextRow("Extra args", L, Y(152.f + extra), s->videoArgs,
		[s](std::string const& text) { s->videoArgs = text; commit(); });

	this->addStepperRow("Fade in", L, Y(189.f + extra),
		[s] { return s->fadeIn <= 0.f ? std::string("off") : fmt::format("{:.1f}s", s->fadeIn); },
		[s](int d) { s->fadeIn = std::clamp(s->fadeIn + d * 0.5f, 0.f, 30.f); commit(); });

	// Правая колонка: аудио
	this->addToggleRow("No audio", R, Y(59.f + extra), s->noAudio,
		[s](bool on) { s->noAudio = on; commit(); });

	this->addStepperRow("Codec", R, Y(90.f + extra),
		[s] { return s->audioCodec; },
		[s](int d) { cycle(s->audioCodec, kAudioCodecs, d); commit(); });

	this->addStepperRow("Bitrate", R, Y(121.f + extra),
		[s] { return fmt::format("{} kbps", s->audioBitrateKbps); },
		[s](int d) { cycle(s->audioBitrateKbps, kAudioBitrates, d); commit(); });

	this->addTextRow("Extra args", R, Y(152.f + extra), s->audioArgs,
		[s](std::string const& text) { s->audioArgs = text; commit(); });

	this->addStepperRow("Fade out", R, Y(189.f + extra),
		[s] { return s->fadeOut <= 0.f ? std::string("off") : fmt::format("{:.1f}s", s->fadeOut); },
		[s](int d) { s->fadeOut = std::clamp(s->fadeOut + d * 0.5f, 0.f, 30.f); commit(); });

	// Кнопка запуска записи: стартуем, закрываем окно и перезапускаем уровень,
	// потому что звук берётся с начала трека
	auto recSprite = ButtonSprite::create("Start recording");
	recSprite->setScale(0.75f);
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
	m_buttonMenu->addChildAtPosition(recBtn, Anchor::Center, {0.f, Y(239.f + extra)});

	return true;
}

void SettingsPopup::addResolutionRow(float y) {
	auto s = &RenderSettings::current();
	auto sprites = std::make_shared<std::vector<ButtonSprite*>>();

	for (size_t i = 0; i < kResolutions.size(); ++i) {
		bool selected = s->videoHeight == kResolutions[i].second;
		auto spr = ButtonSprite::create(
			kResolutions[i].first, 0, false, "goldFont.fnt",
			selected ? "GJ_button_01.png" : "GJ_button_04.png", 0.f, 0.5f
		);
		spr->setScale(0.75f); // компактные кнопки, чтобы не перекрывали заголовки
		sprites->push_back(spr);

		auto btn = CCMenuItemExt::createSpriteExtra(spr, [=](auto*) {
			s->videoHeight = kResolutions[i].second;
			commit();
			// Подсвечиваем выбранную кнопку, остальные серые
			for (size_t j = 0; j < sprites->size(); ++j) {
				(*sprites)[j]->updateBGImage(j == i ? "GJ_button_01.png" : "GJ_button_04.png");
			}
		});
		m_buttonMenu->addChildAtPosition(btn, Anchor::Center, {-144.f + 72.f * static_cast<float>(i), y});
	}
}

void SettingsPopup::addHeader(char const* text, float cx, float y, ccColor3B color) {
	auto label = CCLabelBMFont::create(text, "goldFont.fnt");
	label->setScale(0.55f);
	label->setColor(color);
	m_mainLayer->addChildAtPosition(label, Anchor::Center, {cx, y});
}

void SettingsPopup::addStepperRow(
	char const* title, float cx, float y,
	std::function<std::string()> getText,
	std::function<void(int)> step
) {
	// Подпись над значением
	auto name = CCLabelBMFont::create(title, "bigFont.fnt");
	name->setScale(0.3f);
	name->setColor(kLabel);
	m_mainLayer->addChildAtPosition(name, Anchor::Center, {cx, y + 11.f});

	// Значение между стрелками
	auto value = CCLabelBMFont::create(getText().c_str(), "bigFont.fnt");
	value->setColor(kCyan);
	value->limitLabelWidth(88.f, 0.4f, 0.2f);
	m_mainLayer->addChildAtPosition(value, Anchor::Center, {cx, y - 6.f});

	auto makeArrow = [](bool pointRight) {
		auto spr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
		spr->setScale(0.4f);
		spr->setFlipX(pointRight);
		return spr;
	};

	auto left = CCMenuItemExt::createSpriteExtra(makeArrow(false), [=](auto*) {
		step(-1);
		value->setString(getText().c_str());
		value->limitLabelWidth(88.f, 0.4f, 0.2f);
	});
	auto right = CCMenuItemExt::createSpriteExtra(makeArrow(true), [=](auto*) {
		step(1);
		value->setString(getText().c_str());
		value->limitLabelWidth(88.f, 0.4f, 0.2f);
	});

	m_buttonMenu->addChildAtPosition(left, Anchor::Center, {cx - 62.f, y - 6.f});
	m_buttonMenu->addChildAtPosition(right, Anchor::Center, {cx + 62.f, y - 6.f});
}

void SettingsPopup::addTextRow(
	char const* title, float cx, float y, std::string const& initial,
	std::function<void(std::string const&)> onChange
) {
	auto name = CCLabelBMFont::create(title, "bigFont.fnt");
	name->setScale(0.3f);
	name->setColor(kLabel);
	m_mainLayer->addChildAtPosition(name, Anchor::Center, {cx, y + 11.f});

	auto input = TextInput::create(150.f, "optional", "chatFont.fnt");
	input->setMaxCharCount(300);
	input->setString(initial, false);
	input->setCallback([onChange](std::string const& text) { onChange(text); });
	m_mainLayer->addChildAtPosition(input, Anchor::Center, {cx, y - 8.f});
}

void SettingsPopup::addToggleRow(
	char const* title, float cx, float y, bool initial, std::function<void(bool)> onChange
) {
	auto name = CCLabelBMFont::create(title, "bigFont.fnt");
	name->setScale(0.3f);
	name->setColor(kLabel);
	m_mainLayer->addChildAtPosition(name, Anchor::Center, {cx, y + 11.f});

	auto toggler = CCMenuItemExt::createTogglerWithStandardSprites(0.5f, [onChange](CCMenuItemToggler* t) {
		// Колбэк вызывается до переключения, поэтому новое состояние = !isToggled()
		onChange(!t->isToggled());
	});
	toggler->toggle(initial);
	m_buttonMenu->addChildAtPosition(toggler, Anchor::Center, {cx, y - 6.f});
}

} // namespace sr
