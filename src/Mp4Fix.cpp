#include "Mp4Fix.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

namespace sr {
namespace {

using Bytes = std::vector<uint8_t>;

struct Box {
	size_t off = 0;  // начало бокса в буфере
	size_t size = 0; // полный размер
	size_t hdr = 8;  // размер заголовка
	char type[4] = {0, 0, 0, 0};
	bool is(char const* t) const { return std::memcmp(type, t, 4) == 0; }
};

uint32_t rd32(Bytes const& b, size_t o) {
	return (uint32_t(b[o]) << 24) | (uint32_t(b[o + 1]) << 16) | (uint32_t(b[o + 2]) << 8) | b[o + 3];
}

void wr32(Bytes& b, size_t o, uint32_t v) {
	b[o] = uint8_t(v >> 24); b[o + 1] = uint8_t(v >> 16); b[o + 2] = uint8_t(v >> 8); b[o + 3] = uint8_t(v);
}

// Боксы на уровне [s, e) буфера (только 32-битные размеры)
std::vector<Box> listBoxes(Bytes const& b, size_t s, size_t e) {
	std::vector<Box> out;
	size_t o = s;
	while (o + 8 <= e) {
		Box box;
		box.off = o;
		box.size = rd32(b, o);
		std::memcpy(box.type, &b[o + 4], 4);
		if (box.size < 8 || o + box.size > e) break;
		out.push_back(box);
		o += box.size;
	}
	return out;
}

// Ищет цепочку вложенных боксов по пути. extra[i] - сколько байт после заголовка
// i-го бокса идёт служебных полей до дочерних боксов
bool findChain(Bytes const& b, size_t s, size_t e, char const* const* path,
               size_t const* extra, size_t count, std::vector<Box>& chain) {
	for (auto const& box : listBoxes(b, s, e)) {
		if (!box.is(path[0])) continue;
		chain.push_back(box);
		if (count == 1) return true;
		if (findChain(b, box.off + box.hdr + extra[0], box.off + box.size, path + 1, extra + 1, count - 1, chain)) {
			return true;
		}
		chain.pop_back();
	}
	return false;
}

// Длина дескриптора MPEG-4: 1..4 байта по 7 бит. Возвращает false при ошибке
bool readDescLen(Bytes const& b, size_t& pos, size_t end, size_t& len) {
	len = 0;
	for (int i = 0; i < 4; ++i) {
		if (pos >= end) return false;
		uint8_t c = b[pos++];
		len = (len << 7) | (c & 0x7F);
		if (!(c & 0x80)) return true;
	}
	return false;
}

} // namespace

bool fixMp4AudioConfig(std::filesystem::path const& path) {
	std::error_code ec;
	auto fileSize = std::filesystem::file_size(path, ec);
	if (ec || fileSize < 32) return false;

	// Верхний уровень: ищем moov. Файл пишется без faststart, moov идёт последним,
	// поэтому правим только его хвост и не трогаем смещения данных.
	std::ifstream in(path, std::ios::binary);
	if (!in) return false;

	uint64_t pos = 0;
	uint64_t moovOff = 0, moovSize = 0;
	while (pos + 8 <= fileSize) {
		uint8_t h[8];
		in.seekg(static_cast<std::streamoff>(pos));
		if (!in.read(reinterpret_cast<char*>(h), 8)) return false;
		uint64_t size = (uint32_t(h[0]) << 24) | (uint32_t(h[1]) << 16) | (uint32_t(h[2]) << 8) | h[3];
		if (size == 1) {
			uint8_t big[8];
			if (!in.read(reinterpret_cast<char*>(big), 8)) return false;
			size = 0;
			for (int i = 0; i < 8; ++i) size = (size << 8) | big[i];
		} else if (size == 0) {
			size = fileSize - pos;
		}
		if (size < 8 || pos + size > fileSize) return false;
		if (std::memcmp(h + 4, "moov", 4) == 0) { moovOff = pos; moovSize = size; }
		pos += size;
	}
	if (moovSize == 0 || moovOff + moovSize != fileSize) return false; // moov не в конце: не трогаем
	if (moovSize > 64ull * 1024 * 1024) return false;

	Bytes moov(static_cast<size_t>(moovSize));
	in.clear();
	in.seekg(static_cast<std::streamoff>(moovOff));
	if (!in.read(reinterpret_cast<char*>(moov.data()), static_cast<std::streamsize>(moovSize))) return false;
	in.close();

	// Находим звуковую дорожку: trak -> mdia -> hdlr с типом 'soun'
	std::vector<Box> chain; // moov, trak, mdia, minf, stbl, stsd, mp4a, esds
	for (auto const& trak : listBoxes(moov, 8, moov.size())) {
		if (!trak.is("trak")) continue;

		std::vector<Box> hdlrChain;
		char const* hp[] = {"mdia", "hdlr"};
		size_t hx[] = {0};
		if (!findChain(moov, trak.off + 8, trak.off + trak.size, hp, hx, 2, hdlrChain)) continue;
		auto const& hdlr = hdlrChain.back();
		if (hdlr.off + 20 > moov.size() || std::memcmp(&moov[hdlr.off + 16], "soun", 4) != 0) continue;

		char const* ap[] = {"mdia", "minf", "stbl", "stsd", "mp4a", "esds"};
		size_t ax[] = {0, 0, 0, 8, 28};
		std::vector<Box> sub;
		if (!findChain(moov, trak.off + 8, trak.off + trak.size, ap, ax, 6, sub)) continue;

		chain.push_back(Box{0, moov.size(), 8, {'m', 'o', 'o', 'v'}});
		chain.push_back(trak);
		chain.insert(chain.end(), sub.begin(), sub.end());
		break;
	}
	if (chain.size() != 8) return false;

	Box const& mp4a = chain[6];
	Box const& esds = chain[7];

	// Частота и число каналов берём из AudioSampleEntry
	if (mp4a.off + mp4a.hdr + 28 > moov.size()) return false;
	size_t se = mp4a.off + mp4a.hdr;
	unsigned channels = (unsigned(moov[se + 16]) << 8) | moov[se + 17];
	unsigned sampleRate = (unsigned(moov[se + 24]) << 8) | moov[se + 25]; // целая часть 16.16
	if (channels == 0 || channels > 7) return false;

	static constexpr unsigned kRates[] = {96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050,
	                                      16000, 12000, 11025, 8000, 7350};
	int rateIndex = -1;
	for (int i = 0; i < 13; ++i) if (kRates[i] == sampleRate) rateIndex = i;
	if (rateIndex < 0) return false;

	// Разбираем esds: version/flags(4), ES_Descriptor(0x03)
	size_t p = esds.off + esds.hdr + 4;
	size_t esdsEnd = esds.off + esds.size;
	if (p >= esdsEnd || moov[p] != 0x03) return false;
	++p;
	size_t esLen = 0;
	if (!readDescLen(moov, p, esdsEnd, esLen)) return false;
	if (p + 3 > esdsEnd) return false;
	uint8_t esId0 = moov[p], esId1 = moov[p + 1];
	if (moov[p + 2] & 0xE0) return false; // есть зависимости потоков: структура нестандартная
	p += 3;
	if (p >= esdsEnd || moov[p] != 0x04) return false;
	++p;
	size_t dcdLen = 0;
	if (!readDescLen(moov, p, esdsEnd, dcdLen)) return false;
	if (dcdLen < 13 || p + dcdLen > esdsEnd) return false;
	if (dcdLen > 13) return false; // внутри уже есть DecoderSpecificInfo: файл в порядке

	// Собираем новый esds с AudioSpecificConfig (AAC-LC)
	uint8_t asc0 = uint8_t((2 << 3) | (rateIndex >> 1));
	uint8_t asc1 = uint8_t(((rateIndex & 1) << 7) | (channels << 3));

	Bytes dcd = {0x04, 17};
	dcd.insert(dcd.end(), moov.begin() + p, moov.begin() + p + 13); // тип, буфер, битрейты как были
	dcd.insert(dcd.end(), {0x05, 0x02, asc0, asc1});

	Bytes es = {0x03, uint8_t(3 + dcd.size() + 3), esId0, esId1, 0x00};
	es.insert(es.end(), dcd.begin(), dcd.end());
	es.insert(es.end(), {0x06, 0x01, 0x02}); // SLConfig

	Bytes box(12, 0);
	wr32(box, 0, uint32_t(12 + es.size()));
	std::memcpy(&box[4], "esds", 4);
	box.insert(box.end(), es.begin(), es.end());

	// Подменяем esds и обновляем размеры всех родителей
	long long delta = static_cast<long long>(box.size()) - static_cast<long long>(esds.size);
	Bytes patched;
	patched.reserve(moov.size() + 16);
	patched.insert(patched.end(), moov.begin(), moov.begin() + esds.off);
	patched.insert(patched.end(), box.begin(), box.end());
	patched.insert(patched.end(), moov.begin() + esds.off + esds.size, moov.end());
	for (size_t i = 0; i < 7; ++i) {
		wr32(patched, chain[i].off, uint32_t(static_cast<long long>(chain[i].size) + delta));
	}

	// Пишем moov обратно на то же место (он последний в файле), размер файла подгоняем
	{
		std::fstream out(path, std::ios::binary | std::ios::in | std::ios::out);
		if (!out) return false;
		out.seekp(static_cast<std::streamoff>(moovOff));
		out.write(reinterpret_cast<char const*>(patched.data()), static_cast<std::streamsize>(patched.size()));
		out.flush();
		if (!out) return false;
	}
	std::filesystem::resize_file(path, moovOff + patched.size(), ec);
	return !ec;
}

} // namespace sr
