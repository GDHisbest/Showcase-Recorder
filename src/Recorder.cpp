#include "Recorder.hpp"
#include "RenderSettings.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>

using namespace geode::prelude;

namespace sr {

Recorder& Recorder::get() {
	static Recorder instance;
	return instance;
}

std::filesystem::path Recorder::findSongPath(GJGameLevel* level) {
	if (!level) return {};

	std::string path;
	if (level->m_songID > 0) {
		// Музыка с Newgrounds / из библиотеки: лежит в папке скачанных песен
		path = MusicDownloadManager::sharedState()->pathForSong(level->m_songID);
	} else {
		// Официальный трек, файл в ресурсах игры
		auto name = LevelTools::getAudioFileName(level->m_audioTrack);
		path = CCFileUtils::sharedFileUtils()->fullPathForFilename(name.c_str(), false);
	}
	return path;
}

bool Recorder::start() {
	if (m_recording) return false;

	auto& s = RenderSettings::current();
	s.sanitize();

	// Файлы лежат в папке сохранений мода: .../recordings/showcase_<время>.mp4
	auto dir = Mod::get()->getSaveDir() / "recordings";
	std::error_code ec;
	std::filesystem::create_directories(dir, ec);
	auto stamp = static_cast<long long>(std::time(nullptr));
	m_output = dir / fmt::format("showcase_{}.mp4", stamp);

	// Звук: берём музыку уровня, если не включён No audio и файл найден
	m_withAudio = false;
	m_songPath.clear();
	if (!s.noAudio) {
		auto layer = PlayLayer::get();
		m_songPath = findSongPath(layer ? layer->m_level : nullptr);

		std::error_code existsEc;
		if (!m_songPath.empty() && std::filesystem::exists(m_songPath, existsEc)) {
			m_withAudio = true;
		} else {
			log::warn("Showcase Recorder: song file not found ('{}'), recording without audio",
				m_songPath.string());
			Notification::create("Song file not found, no audio", NotificationIcon::Warning)->show();
		}
	}

	// Со звуком сначала пишем видео во временный файл, потом смешиваем с музыкой
	m_videoFile = m_withAudio
		? dir / fmt::format("showcase_{}_video.mp4", stamp)
		: m_output;

	// Разрешение видео подстраивается под телефон: высота как у экрана,
	// ширина по пропорциям игровой картинки, оба значения чётные
	auto director = CCDirector::get();
	auto win = director->getWinSize();
	auto frame = CCEGLView::get()->getFrameSize();
	float shortSide = std::min(frame.width, frame.height);
	if (shortSide < 144.f) shortSide = director->getWinSizeInPixels().height;

	int h = std::clamp(static_cast<int>(shortSide), 144, 4320);
	int w = std::clamp(static_cast<int>(std::lround(h * (win.width / win.height))), 144, 7680);
	m_height = h - h % 2;
	m_width = w - w % 2;

	m_fps = s.fps;
	m_frames = 0;
	m_stopAtFrame = -1;
	m_levelUpdated = false;
	m_ticks = 0;
	m_levelTicks = 0;

	// Offscreen-текстура нужного размера: в неё рисуем уровень для каждого кадра
	m_texture = CCRenderTexture::create(m_width, m_height, kCCTexture2DPixelFormat_RGBA8888);
	if (!m_texture) {
		log::error("Showcase Recorder: failed to create render texture {}x{}", m_width, m_height);
		return false;
	}
	m_texture->retain();

	size_t bytes = static_cast<size_t>(m_width) * m_height * 4;
	m_pixels.assign(bytes, 0);

	// Настройки FFmpeg API (имена полей взяты из README мода, сверить при ошибках сборки)
	using namespace ffmpeg;
	ffmpeg::RenderSettings settings;
	settings.m_pixelFormat = PixelFormat::RGB0; // RGBA из glReadPixels, альфа игнорируется
	settings.m_codec = s.videoCodec;
	settings.m_bitrate = s.videoBitrateKbps * 1000; // в README биты в секунду
	settings.m_width = m_width;
	settings.m_height = m_height;
	settings.m_fps = m_fps;
	settings.m_outputFile = m_videoFile.string();

	// Каждый раз новый объект, чтобы не зависеть от повторного init
	m_recorder = std::make_unique<ffmpeg::Recorder>();
	m_recorder->init(settings);

	m_recording = true;
	log::info("Showcase Recorder: started {}x{} @ {} fps, audio={} -> {}",
		m_width, m_height, m_fps, m_withAudio, m_output.string());
	if (!s.videoArgs.empty() || !s.audioArgs.empty()) {
		// Пока только сохраняются: в API FFmpeg API по README нет полей для аргументов
		log::info("Showcase Recorder: video args '{}', audio args '{}' (not applied yet)",
			s.videoArgs, s.audioArgs);
	}
	return true;
}

void Recorder::stopAfter(float seconds) {
	if (!m_recording) return;
	m_stopAtFrame = m_frames + static_cast<int>(std::lround(seconds * m_fps));
}

void Recorder::endTick() {
	if (!m_recording) return;

	++m_ticks;
	bool updated = m_levelUpdated;
	m_levelUpdated = false;
	if (!updated) return; // пауза или экран без уровня: кадр не пишем
	++m_levelTicks;

	auto layer = PlayLayer::get();
	if (!layer) {
		log::warn("Showcase Recorder: level updated but PlayLayer::get() is null");
		return;
	}
	this->captureFrame(layer);
}

void Recorder::captureFrame(PlayLayer* layer) {
	if (!m_recording || !m_texture || !m_recorder || !layer) return;

	// CCRenderTexture::begin() рисует сцену в масштабе экрана, поэтому при другом
	// размере видео картинка занимает только часть кадра (в левом нижнем углу).
	// Компенсируем масштабом: вписываем экран в кадр целиком, по краям чёрные полосы.
	auto director = CCDirector::get();
	auto win = director->getWinSize();           // в поинтах
	auto winPx = director->getWinSizeInPixels(); // в пикселях экрана

	float wr = winPx.width / static_cast<float>(m_width);
	float hr = winPx.height / static_cast<float>(m_height);
	float s = std::min(1.f / wr, 1.f / hr);
	float marginX = (1.f - wr * s) * 0.5f;
	float marginY = (1.f - hr * s) * 0.5f;
	float tx = marginX * win.width / wr;
	float ty = marginY * win.height / hr;

	m_texture->beginWithClear(0.f, 0.f, 0.f, 1.f);
	kmGLMatrixMode(KM_GL_MODELVIEW);
	kmGLPushMatrix();
	kmGLTranslatef(tx, ty, 0.f);
	kmGLScalef(s, s, 1.f);
	layer->visit();
	kmGLPopMatrix();
	glReadPixels(0, 0, m_width, m_height, GL_RGBA, GL_UNSIGNED_BYTE, m_pixels.data());
	m_texture->end();

	// Переворот строк не делаем: по видео (картинка была вверх ногами)
	// FFmpeg API сам переворачивает кадры, как это принято для OpenGL.
	m_recorder->writeFrame(m_pixels);
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

	log::info("Showcase Recorder: stopped, ticks={}, level ticks={}, frames={} -> {}",
		m_ticks, m_levelTicks, m_frames, m_output.string());

	this->finishOutput();

	Notification::create(
		fmt::format("Saved: {}", m_output.filename().string()),
		NotificationIcon::Success
	)->show();
}

void Recorder::finishOutput() {
	if (!m_withAudio) return; // видео уже записано сразу в итоговый файл

	std::error_code ec;

	// Смешиваем видео с музыкой уровня. Вызов блокирующий, на длинных
	// записях игра на пару секунд замирает.
	ffmpeg::AudioMixer::mixVideoAudio(
		m_videoFile.string(), m_songPath.string(), m_output.string()
	);

	if (std::filesystem::exists(m_output, ec) && std::filesystem::file_size(m_output, ec) > 0) {
		std::filesystem::remove(m_videoFile, ec);
	} else {
		// Микс не удался: оставляем хотя бы видео без звука
		log::error("Showcase Recorder: audio mix failed, keeping video without audio");
		std::filesystem::rename(m_videoFile, m_output, ec);
		Notification::create("Audio mix failed, saved without audio", NotificationIcon::Warning)->show();
	}
}

} // namespace sr
