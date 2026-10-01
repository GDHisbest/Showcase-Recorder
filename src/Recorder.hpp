#pragma once

#include <Geode/Geode.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

// Заголовок из мода FFmpeg API (путь взят из его README, при ошибке сверить)
#include <eclipse.ffmpeg-api/include/recorder.hpp>

namespace sr {

// Управляет записью видео:
//  start()        - создаёт FFmpeg Recorder и offscreen-текстуру нужного размера
//  captureFrame() - вызывается один раз за каждый шаг игры, пишет один кадр
//  stop()         - закрывает файл
//
// Время кадра фиксировано (1 / fps), поэтому видео ровное, даже если
// телефон рендерит медленнее реального времени.
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

	void captureFrame(PlayLayer* layer);

private:
	bool m_recording = false;
	int m_width = 0;
	int m_height = 0;
	int m_fps = 60;
	int m_frames = 0;
	int m_stopAtFrame = -1;

	std::unique_ptr<ffmpeg::Recorder> m_recorder;
	cocos2d::CCRenderTexture* m_texture = nullptr;
	std::vector<uint8_t> m_pixels;
	std::vector<uint8_t> m_flipped;
	std::filesystem::path m_output;
};

} // namespace sr
