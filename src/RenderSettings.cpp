#include "RenderSettings.hpp"

#include <Geode/loader/Mod.hpp>
#include <algorithm>

using namespace geode::prelude;

namespace sr {

RenderSettings RenderSettings::load() {
	auto* mod = Mod::get();
	RenderSettings s;

	s.fps = mod->getSavedValue<int>("fps", s.fps);
	s.videoBitrateKbps = mod->getSavedValue<int>("video-bitrate-kbps", s.videoBitrateKbps);
	s.videoCodec = mod->getSavedValue<std::string>("video-codec", s.videoCodec);

	s.noAudio = mod->getSavedValue<bool>("no-audio", s.noAudio);
	s.audioCodec = mod->getSavedValue<std::string>("audio-codec", s.audioCodec);
	s.audioBitrateKbps = mod->getSavedValue<int>("audio-bitrate-kbps", s.audioBitrateKbps);

	s.fadeIn = mod->getSavedValue<float>("fade-in", s.fadeIn);
	s.fadeOut = mod->getSavedValue<float>("fade-out", s.fadeOut);

	s.sanitize();
	return s;
}

void RenderSettings::save() const {
	auto* mod = Mod::get();

	mod->setSavedValue("fps", fps);
	mod->setSavedValue("video-bitrate-kbps", videoBitrateKbps);
	mod->setSavedValue("video-codec", videoCodec);

	mod->setSavedValue("no-audio", noAudio);
	mod->setSavedValue("audio-codec", audioCodec);
	mod->setSavedValue("audio-bitrate-kbps", audioBitrateKbps);

	mod->setSavedValue("fade-in", fadeIn);
	mod->setSavedValue("fade-out", fadeOut);
}

void RenderSettings::sanitize() {
	fps = std::clamp(fps, 1, 240);
	videoBitrateKbps = std::clamp(videoBitrateKbps, 100, 200000);
	audioBitrateKbps = std::clamp(audioBitrateKbps, 32, 512);

	fadeIn = std::max(0.f, fadeIn);
	fadeOut = std::max(0.f, fadeOut);

	if (videoCodec.empty()) videoCodec = "libx264";
	if (audioCodec.empty()) audioCodec = "aac";
}

RenderSettings& RenderSettings::current() {
	// Локальная статика: создаётся при первом вызове, когда мод уже загружен
	static RenderSettings instance = load();
	return instance;
}

} // namespace sr
