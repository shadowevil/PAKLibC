// Scenarios against Amp.pak, an original client PAK: 40 PNG sprites, 4 rects each, colour key 00 00 00 00
#include "PakFixture.h"

namespace {
	bool is_whole_png(const std::vector<uint8_t>& image) {
		static const uint8_t signature[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
		return image.size() >= 20 && std::memcmp(image.data(), signature, 8) == 0 &&
			std::memcmp(image.data() + image.size() - 8, "IEND", 4) == 0;
	}
}

TEST_CASE("Amp.pak: loadpak reads all 40 sprites as whole PNGs") {
	const PAKLib::pak pak_file = PAKLib::loadpak(fixture::amp_pak());
	REQUIRE(pak_file.sprite_count == 40);
	REQUIRE(pak_file.sprites.size() == 40);
	CHECK(pak_file.sprite_entries.size() == 40);
	for (const PAKLib::sprite& s : pak_file.sprites) {
		CHECK(s.sprite_rectangles.size() == 4);
		CHECK(is_whole_png(s.image_data));
	}
}

TEST_CASE("Amp.pak: original 00 00 00 00 colour keys read as unset") {
	const PAKLib::pak pak_file = PAKLib::loadpak(fixture::amp_pak());
	for (const PAKLib::sprite& s : pak_file.sprites) {
		CHECK(!s.has_color_key());
		CHECK(s.get_color_key() == 0);
	}
}

TEST_CASE("Amp.pak: every loader returns the same sprites as loadpak") {
	const std::string path = fixture::amp_pak();
	const PAKLib::pak reference = PAKLib::loadpak(path);
	for (const fixture::full_loader& loader : fixture::full_loaders()) {
		const PAKLib::pak other = loader.load(path);
		REQUIRE(other.sprites.size() == reference.sprites.size());
		for (size_t i = 0; i < reference.sprites.size(); ++i)
			CHECK(fixture::same_sprite(other.sprites[i], reference.sprites[i]));
	}
	for (const fixture::sprite_loader& loader : fixture::sprite_loaders()) {
		for (size_t i = 0; i < reference.sprites.size(); ++i)
			CHECK(fixture::same_sprite(loader.load(path, i), reference.sprites[i]));
	}
}

TEST_CASE("Amp.pak: table entries tile the file from the end of the table to the end of the file") {
	const std::string path = fixture::amp_pak();
	const PAKLib::pak pak_file = PAKLib::loadpak(path);
	const std::vector<PAKLib::sprite_entry>& entries = pak_file.sprite_entries;
	REQUIRE(!entries.empty());
	CHECK(entries[0].offset == 24 + 8 * entries.size());
	for (size_t i = 0; i + 1 < entries.size(); ++i)
		CHECK(entries[i].offset + entries[i].size == entries[i + 1].offset);
	CHECK(entries.back().offset + entries.back().size == fixture::read(path).size());
}

TEST_CASE("Paths: a relative path is opened and stored exactly as given") {
	const std::string relative = fixture::amp_pak();
	if (fixture::is_absolute(relative)) {
		std::printf("    note: data_dir is absolute, so the relative-path scenario is skipped\n");
		return;
	}
	for (const fixture::full_loader& loader : fixture::full_loaders()) {
		const PAKLib::pak pak_file = loader.load(relative);
		CHECK(pak_file.pak_file_path == relative);
		CHECK(pak_file.sprites.size() == 40);
	}
	CHECK_NOTHROW(PAKLib::get_sprite(relative, 0));
	CHECK_NOTHROW(PAKLib::get_sprite_fast(relative, 0));
}

TEST_CASE("Paths: an absolute path is opened and stored exactly as given") {
	const std::string absolute = fixture::absolute(fixture::amp_pak());
	for (const fixture::full_loader& loader : fixture::full_loaders()) {
		const PAKLib::pak pak_file = loader.load(absolute);
		CHECK(pak_file.pak_file_path == absolute);
		CHECK(pak_file.sprites.size() == 40);
	}
	CHECK_NOTHROW(PAKLib::get_sprite(absolute, 39));
	CHECK_NOTHROW(PAKLib::get_sprite_fast(absolute, 39));
}

TEST_CASE("Paths: forward and back slashes both work") {
	std::string backslashed = fixture::amp_pak();
	for (char& ch : backslashed)
		if (ch == '/')
			ch = '\\';
#if defined(_WIN32)
	CHECK(PAKLib::loadpak(backslashed).sprites.size() == 40);
#endif
	CHECK(PAKLib::loadpak(fixture::amp_pak()).sprites.size() == 40);
}
