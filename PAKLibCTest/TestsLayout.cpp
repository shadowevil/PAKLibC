// Scenarios on the on-disk layout, using PAKs built byte by byte by the fixture
#include "PakFixture.h"

TEST_CASE("Layout: struct sizes match the on-disk records") {
	CHECK(sizeof(PAKLib::file_header) == 20);
	CHECK(sizeof(PAKLib::sprite_header) == 100);
	CHECK(sizeof(PAKLib::sprite_entry) == 8);
	CHECK(sizeof(PAKLib::sprite_rect) == 12);
}

TEST_CASE("Layout: rect fields keep unsigned x/y/width/height and signed pivots") {
	std::vector<fixture::sprite_spec> specs(1);
	specs[0].rects = {
		{ 0, 0, 1, 1, 0, 0 },
		{ 65535, 40000, 40000, 65535, -5, -32768 },
		{ 12, 34, 56, 78, 32767, 1 },
	};
	const std::string path = fixture::write("layout_rects.pak", fixture::build_pak(specs).data);
	for (const fixture::full_loader& loader : fixture::full_loaders()) {
		const PAKLib::pak pak_file = loader.load(path);
		REQUIRE(pak_file.sprites.size() == 1);
		const std::vector<PAKLib::sprite_rect>& rects = pak_file.sprites[0].sprite_rectangles;
		REQUIRE(rects.size() == 3);
		CHECK(rects[1].x == 65535);
		CHECK(rects[1].y == 40000);
		CHECK(rects[1].width == 40000);
		CHECK(rects[1].height == 65535);
		CHECK(rects[1].pivotX == -5);
		CHECK(rects[1].pivotY == -32768);
		CHECK(rects[2].pivotX == 32767);
		CHECK(rects[2].pivotY == 1);
	}
}

TEST_CASE("Layout: header, count and table are read as written") {
	const fixture::pak_layout layout = fixture::build_pak(fixture::sprites(3));
	const std::string path = fixture::write("layout_table.pak", layout.data);
	const PAKLib::pak pak_file = PAKLib::loadpak(path);
	CHECK(std::memcmp(pak_file.header.signature, "<Pak file header>", 17) == 0);
	for (size_t i = 0; i < 3; ++i)
		CHECK(std::memcmp(pak_file.sprites[i].header.signature, "<Sprite File Header>", 20) == 0);
	for (const fixture::full_loader& loader : fixture::full_loaders()) {
		const PAKLib::pak other = loader.load(path);
		REQUIRE(other.sprite_count == 3);
		REQUIRE(other.sprite_entries.size() == 3);
		for (size_t i = 0; i < 3; ++i) {
			const size_t end = i + 1 < 3 ? layout.sprite_offsets[i + 1] : layout.data.size();
			CHECK(other.sprite_entries[i].offset == layout.sprite_offsets[i]);
			CHECK(other.sprite_entries[i].size == end - layout.sprite_offsets[i]);
		}
	}
}

TEST_CASE("Layout: mixed BMP and PNG sprites with different rect counts load identically everywhere") {
	std::vector<fixture::sprite_spec> specs(4);
	specs[0].image = fixture::make_bmp(3, 2, 0xFF, 0x00, 0xFF);
	specs[1].image = fixture::make_png_header(16, 8);
	specs[1].rects.assign(7, fixture::rect{ 1, 2, 3, 4, -1, -2 });
	specs[2].image = fixture::make_bmp(5, 5);
	specs[2].rects.assign(2, fixture::rect{ 9, 9, 9, 9, 9, 9 });
	specs[3].image = fixture::make_png_header(1, 1);
	const std::string path = fixture::write("layout_mixed.pak", fixture::build_pak(specs).data);

	const PAKLib::pak reference = PAKLib::loadpak(path);
	REQUIRE(reference.sprites.size() == 4);
	for (size_t i = 0; i < specs.size(); ++i) {
		CHECK(reference.sprites[i].image_data == specs[i].image);
		CHECK(reference.sprites[i].sprite_rectangles.size() == specs[i].rects.size());
	}
	for (const fixture::full_loader& loader : fixture::full_loaders()) {
		const PAKLib::pak other = loader.load(path);
		REQUIRE(other.sprites.size() == 4);
		for (size_t i = 0; i < 4; ++i)
			CHECK(fixture::same_sprite(other.sprites[i], reference.sprites[i]));
	}
	for (const fixture::sprite_loader& loader : fixture::sprite_loaders())
		for (size_t i = 0; i < 4; ++i)
			CHECK(fixture::same_sprite(loader.load(path, i), reference.sprites[i]));
}

TEST_CASE("Layout: the 80 sprite-header padding bytes are not checked") {
	fixture::pak_layout layout = fixture::build_pak(fixture::sprites(2));
	for (size_t i = 0; i < 80; ++i)
		layout.data[layout.sprite_offsets[1] + 20 + i] = static_cast<uint8_t>(0xA0 + (i % 16));
	const std::string path = fixture::write("layout_padding.pak", layout.data);
	CHECK(PAKLib::loadpak(path).sprites.size() == 2);
	CHECK_NOTHROW(PAKLib::get_sprite(path, 1));
}
