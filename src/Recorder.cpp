#include "Recorder.hpp"
#include "RenderSettings.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <fstream>

using namespace geode::prelude;

namespace sr {

Recorder& Recorder::get() {
	static Recorder instance;
	return instance;
}

bool Recorder::canDecode(std::string const& path) {
	if (path.empty()) return false;

	auto engine = FMODAudioEngine::sharedEngine();
	if (!engine || !engine->m_system) return false;

	FMOD::Sound* sound = nullptr;
	auto result = engine->m_system->createSound(
		path.c_str(), FMOD_DEFAULT | FMOD_CREATESTREAM | FMOD_OPENONLY, nullptr, &sound
	);
	if (result == FMOD_OK && sound) {
		sound->release();
		return true;
	}
	return false;
}

std::filesystem::path Recorder::findSongPath(GJGameLevel* level) {
	std::vector<std::string> candidates;

	if (level) {
		std::string base;
		if (level->m_songID > 0) {
			// Музыка с Newgrounds / из библиотеки
			base = MusicDownloadManager::sharedState()->pathForSong(level->m_songID);
		} else {
			// Официальный трек
			base = LevelTools::getAudioFileName(level->m_audioTrack);
		}

		if (!base.empty()) {
			auto files = CCFileUtils::sharedFileUtils();
			candidates.push_back(base);
			candidates.push_back(files->fullPathForFilename(base.c_str(), false));
			candidates.push_back(files->fullPathForFilename(
				std::filesystem::path(base).filename().string().c_str(), false
			));
		}
		log::info("Showcase Recorder: level song id {}, official track {}, base path '{}'",
			level->m_songID, level->m_audioTrack, base);
	}

	// Запасной вариант: файл, который игра прямо сейчас проигрывает как музыку фона.
	// Игра уже умеет его открыть, поэтому путь точно подходит.
	auto engine = FMODAudioEngine::sharedEngine();
	if (engine && engine->m_backgroundMusicChannel) {
		FMOD::Sound* playing = nullptr;
		engine->m_backgroundMusicChannel->getCurrentSound(&playing);
		if (playing) {
			char name[1024] = {0};
			playing->getName(name, sizeof(name));
			if (name[0]) candidates.push_back(name);
		}
	}

	for (auto const& candidate : candidates) {
		bool ok = canDecode(candidate);
		log::info("Showcase Recorder: song candidate '{}' -> {}", candidate, ok ? "ok" : "not usable");
		if (ok) return candidate;
	}
	return {};
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

		if (!m_songPath.empty()) {
			m_withAudio = true;
		} else {
			log::warn("Showcase Recorder: could not open the level song, recording without audio");
			Notification::create("Could not open the song, recording without audio",
				NotificationIcon::Warning)->show();
		}
	}

	// Со звуком сначала пишем видео во временный файл, потом смешиваем с музыкой
	m_videoFile = m_withAudio
		? dir / fmt::format("showcase_{}_video.mp4", stamp)
		: m_output;

#ifdef GEODE_IS_DESKTOP
	// ПК: разрешение выбирается в настройках (720p ... 8K), ширина 16:9.
	// Если окно игры другого формата, картинка вписывается с чёрными полосами.
	int h = std::clamp(s.videoHeight, 144, 4320);
	int w = static_cast<int>(std::lround(h * 16.0 / 9.0));
	m_height = h - h % 2;
	m_width = w - w % 2;
#else
	// Android: разрешение подстраивается под телефон: высота как у экрана,
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
#endif

	m_fps = s.fps;
	m_frames = 0;
	m_stopAtFrame = -1;
	m_levelUpdated = false;
	m_ticks = 0;
	m_levelTicks = 0;
	m_havePercent = false;

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

	// Запоминаем процент прохождения для проверки скорости
	m_lastPercent = layer->getCurrentPercent();
	if (!m_havePercent) {
		m_startPercent = m_lastPercent;
		m_havePercent = true;
	}

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

	// Проверка скорости: за сколько секунд ВИДЕО набралось бы 100% уровня.
	// Если это число сильно больше настоящей длины уровня, игра в видео идёт медленнее.
	double videoSeconds = static_cast<double>(m_frames) / static_cast<double>(m_fps);
	double progress = static_cast<double>(m_lastPercent - m_startPercent);
	if (m_havePercent && progress > 0.5 && videoSeconds > 0.0) {
		double fullLength = videoSeconds * 100.0 / progress;
		log::info("Showcase Recorder: speed check, {:.2f}% in {:.2f}s of video -> full level {:.0f}s",
			progress, videoSeconds, fullLength);
		Notification::create(
			fmt::format("Level length in video: {}:{:02}", static_cast<int>(fullLength) / 60,
				static_cast<int>(fullLength) % 60),
			NotificationIcon::Info, 6.f
		)->show();
	}

	this->finishOutput();

	Notification::create(
		fmt::format("Saved: {}", m_output.filename().string()),
		NotificationIcon::Success
	)->show();
}

bool Recorder::buildAudioWav(std::filesystem::path const& wavPath, double seconds) {
	auto engine = FMODAudioEngine::sharedEngine();
	if (!engine || !engine->m_system) return false;

	// Открываем файл музыки только для чтения PCM (без воспроизведения)
	FMOD::Sound* sound = nullptr;
	auto result = engine->m_system->createSound(
		m_songPath.string().c_str(),
		FMOD_DEFAULT | FMOD_CREATESTREAM | FMOD_OPENONLY,
		nullptr, &sound
	);
	if (result != FMOD_OK || !sound) {
		log::error("Showcase Recorder: cannot open song for decoding ({})", static_cast<int>(result));
		return false;
	}

	FMOD_SOUND_FORMAT format = FMOD_SOUND_FORMAT_NONE;
	int channels = 0;
	int bits = 0;
	float freq = 0.f;
	sound->getFormat(nullptr, &format, &channels, &bits);
	sound->getDefaults(&freq, nullptr);

	int bytesPerSample = 0;
	if (format == FMOD_SOUND_FORMAT_PCM16) bytesPerSample = 2;
	else if (format == FMOD_SOUND_FORMAT_PCMFLOAT) bytesPerSample = 4;

	if (bytesPerSample == 0 || channels < 1 || channels > 2 || freq < 8000.f) {
		log::error("Showcase Recorder: unsupported song format {} ch {} freq {}",
			static_cast<int>(format), channels, freq);
		sound->release();
		return false;
	}

	int sampleRate = static_cast<int>(std::lround(freq));
	size_t totalFrames = static_cast<size_t>(std::llround(seconds * sampleRate));
	std::vector<float> pcm(totalFrames * 2, 0.f); // стерео, остаток после конца песни = тишина

	// Читаем кусками строго с начала песни и сразу переводим в float-стерео
	constexpr unsigned int kChunkFrames = 4096;
	std::vector<uint8_t> chunk(static_cast<size_t>(kChunkFrames) * channels * bytesPerSample);
	size_t written = 0;
	while (written < totalFrames) {
		unsigned int want = static_cast<unsigned int>(
			std::min<size_t>(kChunkFrames, totalFrames - written) * channels * bytesPerSample
		);
		unsigned int got = 0;
		auto r = sound->readData(chunk.data(), want, &got);
		unsigned int gotFrames = got / (channels * bytesPerSample);
		for (unsigned int i = 0; i < gotFrames; ++i) {
			float samples[2] = {0.f, 0.f};
			for (int c = 0; c < channels; ++c) {
				size_t offset = (static_cast<size_t>(i) * channels + c) * bytesPerSample;
				if (bytesPerSample == 2) {
					int16_t v;
					std::memcpy(&v, chunk.data() + offset, 2);
					samples[c] = static_cast<float>(v) / 32768.f;
				} else {
					std::memcpy(&samples[c], chunk.data() + offset, 4);
				}
			}
			if (channels == 1) samples[1] = samples[0];
			pcm[(written + i) * 2] = samples[0];
			pcm[(written + i) * 2 + 1] = samples[1];
		}
		written += gotFrames;
		if (r != FMOD_OK || gotFrames == 0) break; // конец файла или ошибка
	}
	sound->release();

	// Fade in / fade out звука (в секундах)
	auto& s = RenderSettings::current();
	size_t fadeInFrames = static_cast<size_t>(std::max(0.f, s.fadeIn) * sampleRate);
	size_t fadeOutFrames = static_cast<size_t>(std::max(0.f, s.fadeOut) * sampleRate);
	for (size_t i = 0; i < totalFrames; ++i) {
		float gain = 1.f;
		if (fadeInFrames > 0 && i < fadeInFrames) {
			gain = std::min(gain, static_cast<float>(i) / static_cast<float>(fadeInFrames));
		}
		if (fadeOutFrames > 0 && totalFrames - 1 - i < fadeOutFrames) {
			gain = std::min(gain, static_cast<float>(totalFrames - 1 - i) / static_cast<float>(fadeOutFrames));
		}
		if (gain < 1.f) {
			pcm[i * 2] *= gain;
			pcm[i * 2 + 1] *= gain;
		}
	}

	// Пишем WAV: 16 бит, стерео
	std::ofstream out(wavPath, std::ios::binary);
	if (!out) return false;

	auto put32 = [&](uint32_t v) { out.write(reinterpret_cast<char const*>(&v), 4); };
	auto put16 = [&](uint16_t v) { out.write(reinterpret_cast<char const*>(&v), 2); };

	uint32_t dataBytes = static_cast<uint32_t>(totalFrames * 2 * 2);
	out.write("RIFF", 4);
	put32(36 + dataBytes);
	out.write("WAVEfmt ", 8);
	put32(16);
	put16(1);                                   // PCM
	put16(2);                                   // каналов
	put32(static_cast<uint32_t>(sampleRate));
	put32(static_cast<uint32_t>(sampleRate) * 4);
	put16(4);                                   // байт на кадр
	put16(16);                                  // бит на сэмпл
	out.write("data", 4);
	put32(dataBytes);

	std::vector<int16_t> block(2048);
	for (size_t i = 0; i < pcm.size(); i += block.size()) {
		size_t n = std::min(block.size(), pcm.size() - i);
		for (size_t j = 0; j < n; ++j) {
			float v = std::clamp(pcm[i + j], -1.f, 1.f);
			block[j] = static_cast<int16_t>(std::lround(v * 32767.f));
		}
		out.write(reinterpret_cast<char const*>(block.data()), static_cast<std::streamsize>(n * 2));
	}

	log::info("Showcase Recorder: audio wav {:.2f}s, {} Hz, song frames read {}/{}",
		seconds, sampleRate, written, totalFrames);
	return out.good();
}

void Recorder::finishOutput() {
	if (!m_withAudio) return; // видео уже записано сразу в итоговый файл

	std::error_code ec;

	// Миксер сжимает звук под длину видео, поэтому даём ему WAV ровно такой же длины:
	// длина = число записанных кадров / fps, музыка с самого начала, без ускорения.
	double videoSeconds = static_cast<double>(m_frames) / static_cast<double>(m_fps);
	auto wavPath = m_videoFile;
	wavPath.replace_extension(".wav");

	bool wavOk = m_frames > 0 && this->buildAudioWav(wavPath, videoSeconds);
	if (wavOk) {
		// Вызов блокирующий, на длинных записях игра на пару секунд замирает
		ffmpeg::AudioMixer::mixVideoAudio(
			m_videoFile.string(), wavPath.string(), m_output.string()
		);
	}
	std::filesystem::remove(wavPath, ec);

	if (wavOk && std::filesystem::exists(m_output, ec) && std::filesystem::file_size(m_output, ec) > 0) {
		std::filesystem::remove(m_videoFile, ec);
	} else {
		// Микс не удался: оставляем хотя бы видео без звука
		log::error("Showcase Recorder: audio mix failed, keeping video without audio");
		std::filesystem::rename(m_videoFile, m_output, ec);
		Notification::create("Audio mix failed, saved without audio", NotificationIcon::Warning)->show();
	}
}

} // namespace sr
