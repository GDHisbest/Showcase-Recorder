#include "Recorder.hpp"
#include "RenderSettings.hpp"

#include <cmath>
#include <cstring>
#include <ctime>

using namespace geode::prelude;

namespace sr {

Recorder& Recorder::get() {
	static Recorder instance;
	return instance;
}

bool Recorder::start() {
	if (m_recording) return false;

	auto& s = RenderSettings::current();
	s.sanitize();

	// Файлы лежат в папке сохранений мода: .../recordings/showcase_<время>.mp4
	auto dir = Mod::get()->getSaveDir() / "recordings";
	std::error_code ec;
	std::filesystem::create_directories(dir, ec);
	m_output = dir / fmt::format("showcase_{}.mp4", static_cast<long long>(std::time(nullptr)));

	m_width = s.width;
	m_height = s.height;
	m_fps = s.fps;
	m_frames = 0;
	m_stopAtFrame = -1;

	// Offscreen-текстура нужного размера: в неё рисуем уровень для каждого кадра
	m_texture = CCRenderTexture::create(m_width, m_height, kCCTexture2DPixelFormat_RGBA8888);
	if (!m_texture) {
		log::error("Showcase Recorder: failed to create render texture {}x{}", m_width, m_height);
		return false;
	}
	m_texture->retain();

	size_t bytes = static_cast<size_t>(m_width) * m_height * 4;
	m_pixels.assign(bytes, 0);
	m_flipped.assign(bytes, 0);

	// Настройки FFmpeg API (имена полей взяты из README мода, сверить при ошибках сборки)
	using namespace ffmpeg;
	ffmpeg::RenderSettings settings;
	settings.m_pixelFormat = PixelFormat::RGB0; // RGBA из glReadPixels, альфа игнорируется
	settings.m_codec = s.videoCodec;
	settings.m_bitrate = s.videoBitrateKbps * 1000; // в README биты в секунду
	settings.m_width = m_width;
	settings.m_height = m_height;
	settings.m_fps = m_fps;
	settings.m_outputFile = m_output.string();

	// Каждый раз новый объект, чтобы не зависеть от повторного init
	m_recorder = std::make_unique<ffmpeg::Recorder>();
	m_recorder->init(settings);

	m_recording = true;
	log::info("Showcase Recorder: started {}x{} @ {} fps -> {}",
		m_width, m_height, m_fps, m_output.string());
	return true;
}

void Recorder::stopAfter(float seconds) {
	if (!m_recording) return;
	m_stopAtFrame = m_frames + static_cast<int>(std::lround(seconds * m_fps));
}

void Recorder::captureFrame(PlayLayer* layer) {
	if (!m_recording || !m_texture || !m_recorder || !layer) return;

	// Рисуем уровень в offscreen-текстуру и читаем пиксели
	m_texture->begin();
	layer->visit();
	glReadPixels(0, 0, m_width, m_height, GL_RGBA, GL_UNSIGNED_BYTE, m_pixels.data());
	m_texture->end();

	// OpenGL отдаёт строки снизу вверх, а видео ждёт сверху вниз
	size_t row = static_cast<size_t>(m_width) * 4;
	for (int y = 0; y < m_height; ++y) {
		std::memcpy(
			m_flipped.data() + static_cast<size_t>(y) * row,
			m_pixels.data() + static_cast<size_t>(m_height - 1 - y) * row,
			row
		);
	}

	m_recorder->writeFrame(m_flipped);
	++m_frames;

	if (m_stopAtFrame >= 0 && m_frames >= m_stopAtFrame) {
		this->stop();
	}
}

void Recorder::stop() {
	if (!m_recording) return;
	m_recording = false;

	if (m_recorder) {
		m_recorder->stop();
		m_recorder.reset();
	}
	if (m_texture) {
		m_texture->release();
		m_texture = nullptr;
	}
	m_pixels.clear();
	m_pixels.shrink_to_fit();
	m_flipped.clear();
	m_flipped.shrink_to_fit();

	log::info("Showcase Recorder: stopped, {} frames -> {}", m_frames, m_output.string());
	Notification::create(
		fmt::format("Saved: {}", m_output.filename().string()),
		NotificationIcon::Success
	)->show();
}

} // namespace sr
