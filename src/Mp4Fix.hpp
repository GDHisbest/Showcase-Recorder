#pragma once

#include <filesystem>

namespace sr {

// Чинит заголовок аудио в готовом MP4. Микшер FFmpeg API не записывает в файл
// AudioSpecificConfig для AAC (esds без дескриптора 0x05), из-за чего плееры
// (Windows и др.) пишут «формат mp4a не поддерживается». Функция дописывает
// корректный конфиг (AAC-LC, частота и каналы берутся из самого файла).
// Возвращает true, если файл был исправлен. Если файл уже в порядке или его
// структура неожиданная, ничего не трогает и возвращает false.
bool fixMp4AudioConfig(std::filesystem::path const& path);

} // namespace sr
