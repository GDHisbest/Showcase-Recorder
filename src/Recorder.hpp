#pragma once

#include <Geode/Geode.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

// Заголовок из мода FFmpeg API (путь взят из его README, при ошибке сверить)
#include <eclipse.ffmpeg-api/include/recorder.hpp>
#include <eclipse.ffmpeg-api/include/audio_mixer.hpp>

namespace sr {

// Управляет записью видео:
//  start()        - создаёт FFmpeg Recorder и offscreen-текстуру нужного размера
//  markLevelUpdated() + endTick() - один кадр видео за один тик планировщика
//  stop()         - закрывает файл
//
// Один тик планировщика = ровно 1 / fps секунд игрового времени (хук на
// CCScheduler::update). Кадр пишется один раз за тик, независимо от TPS физики,
// поэтому видео идёт в нормальной скорости, даже если телефон рендерит медленно.
class Recorder {
public:
	static Recorder& get();

	bool isRecording() const { return m_recording; }

	// Шаг игрового времени на один кадр видео
	float stepDt() const { return 1.f / static_cast<float>(m_fps); }

	bool start();
	void stop();

	// Остановить запись через N секунд видео (например, после прохождения уровня)
	void stopAfter(float seconds);

	// Уровень обновился в этом тике (вызывает хук PlayLayer::update)
	void markLevelUpdated() { m_levelUpdated = true; }

	// Конец тика планировщика: если уровень обновился, пишем один кадр
	void endTick();

private:
	void captureFrame(PlayLayer* layer);

	// Находит музыку уровня, которую реально можно открыть (пустой путь, если не нашли)
	static std::filesystem::path findSongPath(GJGameLevel* level);

	// true, если FMOD может открыть этот путь для чтения звука
	static bool canDecode(std::string const& path);

	// Добавляет музыку к записанному видео (или просто переименовывает файл)
	void finishOutput();

	// Декодирует музыку уровня и пишет WAV, длина которого ровно равна длине видео
	// (с затуханием fade in / fade out). Возвращает false при ошибке.
	bool buildAudioWav(std::filesystem::path const& wavPath, double seconds);

	bool m_recording = false;
	bool m_levelUpdated = false;
	int m_width = 0;
	int m_height = 0;
	int m_fps = 60;
	int m_frames = 0;
	int m_stopAtFrame = -1;
	int m_ticks = 0;        // диагностика: тиков планировщика за запись
	int m_levelTicks = 0;   // диагностика: тиков, в которых уровень обновлялся

	std::unique_ptr<ffmpeg::Recorder> m_recorder;
	cocos2d::CCRenderTexture* m_texture = nullptr;
	std::vector<uint8_t> m_pixels;
	std::filesystem::path m_output;     // итоговый файл
	std::filesystem::path m_videoFile;  // видео без звука (если звук будет добавлен)
	std::filesystem::path m_songPath;
	bool m_withAudio = false;

	// Контроль скорости: процент прохождения в первом и последнем кадре видео
	float m_startPercent = 0.f;
	float m_lastPercent = 0.f;
	bool m_havePercent = false;
};

} // namespace sr
