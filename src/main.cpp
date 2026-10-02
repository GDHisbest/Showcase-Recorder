#include <Geode/Geode.hpp>
#include "RenderSettings.hpp"

using namespace geode::prelude;

// Вызывается один раз, когда мод загружен
$on_mod(Loaded) {
	auto& s = sr::RenderSettings::current();
	log::info("Showcase Recorder loaded: {} fps, codec {}", s.fps, s.videoCodec);
}
