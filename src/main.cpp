#include <Geode/Geode.hpp>
#include "RenderSettings.hpp"

using namespace geode::prelude;

// Вызывается один раз, когда мод загружен
$on_mod(Loaded) {
	auto& s = sr::RenderSettings::current();
	log::info("Showcase Recorder loaded: {}x{} @ {} fps, codec {}",
		s.width, s.height, s.fps, s.videoCodec);
}
