#include "Recorder.hpp"
#include "RenderSettings.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iterator>
#include <span>

using namespace geode::prelude;

namespace sr {

Recorder& Recorder::get() {
	static Recorder instance;
	return instance;
}

namespace {

// Читает файл целиком через файловую систему игры (на Android это умеет читать
// и ресурсы внутри APK, куда обычный путь для FMOD не ведёт), затем открывает
// звук FMOD из памяти. data должен жить, пока жив звук.
FMOD::Sound* openSongFromMemory(std::string const& path, std::vector<uint8_t>& data) {
	if (path.empty()) return nullptr;

	auto engine = FMODAudioEngine::sharedEngine();
	if (!engine || !engine->m_system) return nullptr;

	data.clear();
	unsigned long size = 0;
	unsigned char* raw = CCFileUtils::sharedFileUtils()->getFileData(path.c_str(), "rb", &size);
	if (raw && size > 0) {
		data.assign(raw, raw + size);
	}
	delete[] raw;

	if (data.empty()) {
		// Запасной вариант: обычное чтение файла с диска
		std::ifstream in(path, std::ios::binary);
		if (in) {
			data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
		}
	}
	if (data.empty()) return nullptr;

	FMOD_CREATESOUNDEXINFO info{};
	info.cbsize = sizeof(info);
	info.length = static_cast<unsigned int>(data.size());

	FMOD::Sound* sound = nullptr;
	auto result = engine->m_system->createSound(
		reinterpret_cast<char const*>(data.data()),
		FMOD_OPENMEMORY | FMOD_OPENONLY, &info, &sound
	);
	if (result != FMOD_OK || !sound) return nullptr;
	return sound;
}

} // namespace

bool Recorder::canDecode(std::string const& path) {
	std::vector<uint8_t> data;
	auto sound = openSongFromMemory(path, data);
	if (!sound) return false;
	sound->release();
	return true;
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
		// m_backgroundMusicChannel это ChannelGroup, звук берём у его первого канала
		FMOD::Sound* playing = nullptr;
		FMOD::Channel* channel = nullptr;
		int channelCount = 0;
		engine->m_backgroundMusicChannel->getNumChannels(&channelCount);
		if (channelCount > 0) {
			engine->m_backgroundMusicChannel->getChannel(0, &channel);
			if (channel) channel->getCurrentSound(&playing);
		}
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

bool Recorder::buildAudioPcm(std::vector<float>& pcm, int& sampleRate, double seconds) {
	auto engine = FMODAudioEngine::sharedEngine();
	if (!engine || !engine->m_system) return false;

	// Открываем файл музыки только для чтения PCM (без воспроизведения)
	std::vector<uint8_t> songData;
	FMOD::Sound* sound = openSongFromMemory(m_songPath.string(), songData);
	if (!sound) {
		log::error("Showcase Recorder: cannot open song for decoding ('{}')", m_songPath.string());
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

	sampleRate = static_cast<int>(std::lround(freq));
	size_t totalFrames = static_cast<size_t>(std::llround(seconds * sampleRate));
	pcm.assign(totalFrames * 2, 0.f); // стерео, остаток после конца песни = тишина

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

	log::info("Showcase Recorder: audio pcm {:.2f}s, {} Hz, song frames read {}/{}",
		seconds, sampleRate, written, totalFrames);
	return true;
}

namespace {

// Приводит интерливленное стерео-аудио к 44100 Гц (именно в такой частоте
// FFmpeg API кодирует звук), линейной интерполяцией
std::vector<float> resampleTo44100(std::vector<float> const& in, int rate) {
	if (rate == 44100 || in.empty()) return in;

	size_t inFrames = in.size() / 2;
	size_t outFrames = static_cast<size_t>(std::llround(static_cast<double>(inFrames) * 44100.0 / rate));
	std::vector<float> out(outFrames * 2, 0.f);
	double ratio = static_cast<double>(rate) / 44100.0;
	for (size_t i = 0; i < outFrames; ++i) {
		double pos = static_cast<double>(i) * ratio;
		size_t i0 = std::min(static_cast<size_t>(pos), inFrames - 1);
		size_t i1 = std::min(i0 + 1, inFrames - 1);
		float t = static_cast<float>(pos - static_cast<double>(i0));
		for (int c = 0; c < 2; ++c) {
			out[i * 2 + c] = in[i0 * 2 + c] * (1.f - t) + in[i1 * 2 + c] * t;
		}
	}
	return out;
}

// Запасной вариант: WAV 16 бит, стерео, рядом с видео
bool writeWav(std::filesystem::path const& wavPath, std::vector<float> const& pcm, int sampleRate) {
	std::ofstream out(wavPath, std::ios::binary);
	if (!out) return false;

	auto put32 = [&](uint32_t v) { out.write(reinterpret_cast<char const*>(&v), 4); };
	auto put16 = [&](uint16_t v) { out.write(reinterpret_cast<char const*>(&v), 2); };

	uint32_t dataBytes = static_cast<uint32_t>(pcm.size() * 2);
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
	return out.good();
}

} // namespace

void Recorder::finishOutput() {
	if (!m_withAudio) return; // видео уже записано сразу в итоговый файл

	std::error_code ec;

	// mixVideoRaw сам считает частоту звука как «число сэмплов / длина видео»,
	// поэтому даём ему ровно столько звука (44100 Гц, стерео), сколько длится видео:
	// музыка с самого начала, без ускорения и растягивания.
	double videoSeconds = static_cast<double>(m_frames) / static_cast<double>(m_fps);

	std::vector<float> pcm;
	int sampleRate = 0;
	bool pcmOk = m_frames > 0 && this->buildAudioPcm(pcm, sampleRate, videoSeconds);
	if (pcmOk) pcm = resampleTo44100(pcm, sampleRate);

	bool mixed = false;
	if (pcmOk) {
		auto videoSize = std::filesystem::exists(m_videoFile, ec) ? std::filesystem::file_size(m_videoFile, ec) : 0;
		log::info("Showcase Recorder: mixing video '{}' ({} bytes) with {} audio samples",
			m_videoFile.string(), static_cast<unsigned long long>(videoSize), pcm.size());

		// Вызов блокирующий, на длинных записях игра на пару секунд замирает
		auto mixResult = ffmpeg::AudioMixer::mixVideoRaw(
			m_videoFile, std::span<float>(pcm.data(), pcm.size()), m_output
		);
		if (mixResult.isErr()) {
			auto message = fmt::format("{}", mixResult.unwrapErr());
			log::error("Showcase Recorder: mixVideoRaw failed: {}", message);
			if (message.size() > 70) message.resize(70);
			Notification::create("Mix error: " + message, NotificationIcon::Error, 6.f)->show();
		} else {
			mixed = true;
		}
	}

	if (mixed && std::filesystem::exists(m_output, ec) && std::filesystem::file_size(m_output, ec) > 0) {
		std::filesystem::remove(m_videoFile, ec);
		return;
	}

	// Микс не удался: итоговый файл = видео без звука, звук сохраняем рядом WAV-ом,
	// чтобы его можно было соединить вручную
	log::error("Showcase Recorder: audio mix failed, keeping video without audio");
	std::filesystem::remove(m_output, ec);
	std::filesystem::rename(m_videoFile, m_output, ec);
	if (pcmOk) {
		auto wavPath = m_output;
		wavPath.replace_extension(".wav");
		if (writeWav(wavPath, pcm, 44100)) {
			log::info("Showcase Recorder: audio saved separately: {}", wavPath.string());
		}
	}
	Notification::create("Audio mix failed, saved without audio", NotificationIcon::Warning)->show();
}

} // namespace sr
