#pragma once

#include <string>

namespace sr {

// Все настройки рендера в одном месте.
// UI (SettingsPopup) меняет эти поля, Recorder читает их при старте записи.
struct RenderSettings {
	// Видео (разрешение берётся с экрана телефона, см. Recorder::start)
	int videoHeight = 1080;             // только ПК: 720..4320, ширина 16:9. На Android авто
	int fps = 60;                       // FPS видео, не зависит от TPS игры
	int videoBitrateKbps = 8000;
	std::string videoCodec = "libx264"; // позже заменим на список доступных кодеков

	// Аудио
	bool noAudio = false;
	std::string audioCodec = "aac";
	int audioBitrateKbps = 192;

	// Дополнительные аргументы кодирования (строкой, как для ffmpeg)
	std::string videoArgs;
	std::string audioArgs;

	// Fade в секундах (0 = выключено)
	float fadeIn = 0.f;
	float fadeOut = 0.f;

	// Загрузка и сохранение через Mod::getSavedValue / setSavedValue
	static RenderSettings load();
	void save() const;

	// Приводит значения к допустимым (чётные размеры, диапазоны, fade >= 0)
	void sanitize();

	// Общий экземпляр для всего мода (создаётся при первом вызове)
	static RenderSettings& current();
};

} // namespace sr
