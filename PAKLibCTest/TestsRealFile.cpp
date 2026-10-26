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
	std::vector<fixture::full_loader> loaders = fixture::full_loaders();
	loaders.push_back(fixture::strict_loaders()[1]);
	for (const fixture::full_loader& loader : loaders) {
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

namespace {
	// Every reader opens `path` and returns the same sprites as `reference`
	void check_path_loads(const std::string& path, const PAKLib::pak& reference) {
		for (const fixture::full_loader& loader : fixture::full_loaders()) {
			const PAKLib::pak pak_file = loader.load(path);
			REQUIRE(pak_file.sprites.size() == reference.sprites.size());
			for (size_t i = 0; i < reference.sprites.size(); ++i)
				CHECK(fixture::same_sprite(pak_file.sprites[i], reference.sprites[i]));
		}
		for (const fixture::sprite_loader& loader : fixture::sprite_loaders()) {
			CHECK(fixture::same_sprite(loader.load(path, 0), reference.sprites[0]));
			CHECK(fixture::same_sprite(loader.load(path, 39), reference.sprites[39]));
		}
	}
}

TEST_CASE("Paths: a relative path loads the same sprites as the absolute path") {
	const std::string relative = fixture::amp_pak();
	if (fixture::is_absolute(relative)) {
		std::printf("    note: data_dir is absolute, so the relative-path scenario is skipped\n");
		return;
	}
	const PAKLib::pak reference = PAKLib::loadpak(fixture::absolute(relative));
	REQUIRE(reference.sprites.size() == 40);
	check_path_loads(relative, reference);
	check_path_loads("./" + relative, reference);
}

TEST_CASE("Paths: an absolute path loads all 40 sprites") {
	const std::string absolute = fixture::absolute(fixture::amp_pak());
	REQUIRE(fixture::is_absolute(absolute));
	const PAKLib::pak reference = PAKLib::loadpak(fixture::amp_pak());
	REQUIRE(reference.sprites.size() == 40);
	check_path_loads(absolute, reference);
}

TEST_CASE("Paths: forward and back slashes load the same sprites") {
	const PAKLib::pak reference = PAKLib::loadpak(fixture::amp_pak());
	REQUIRE(reference.sprites.size() == 40);
	std::string backslashed = fixture::absolute(fixture::amp_pak());
	for (char& ch : backslashed)
		if (ch == '/')
			ch = '\\';
	std::string forward = backslashed;
	for (char& ch : forward)
		if (ch == '\\')
			ch = '/';
#if defined(_WIN32)
	check_path_loads(backslashed, reference);
#endif
	check_path_loads(forward, reference);
}
