// Scenarios for malformed files: each loader must throw, with a message naming the problem.
// These cover the checks PAK.h makes today; the PAKLib 2.0 read checks are added when ported.
#include "PakFixture.h"

namespace {
	// Every fixture here is malformed except the boundary cases (max_sprites, max_rects, index_range)
	std::string write_bad(const std::string& name, const fixture::bytes& data) {
		return fixture::write(name, data);
	}

	fixture::bytes truncated(fixture::bytes data, size_t keep) {
		data.resize(keep);
		return data;
	}
}

TEST_CASE("Validation: a missing file throws from every loader") {
	const std::string path = fixture::output_dir() + "/does_not_exist.pak";
	for (const fixture::full_loader& loader : fixture::full_loaders())
		CHECK_THROWS_WITH(loader.load(path), "Failed to open PAK file");
	for (const fixture::sprite_loader& loader : fixture::sprite_loaders())
		CHECK_THROWS_WITH(loader.load(path, 0), "Failed to open PAK file");
}

TEST_CASE("Validation: a wrong file header magic is rejected") {
	fixture::pak_layout layout = fixture::build_pak(fixture::sprites(1));
	layout.data[1] = 'Q';
	const std::string path = write_bad("bad_file_magic.pak", layout.data);
	CHECK_THROWS_WITH(PAKLib::loadpak(path), "Invalid PAK file header");
	CHECK_THROWS_WITH(PAKLib::get_sprite(path, 0), "Invalid PAK file header");
}

TEST_CASE("Validation: a sprite count of 0 is rejected") {
	const std::string path = write_bad("zero_sprites.pak", fixture::build_pak({}).data);
	CHECK_THROWS_WITH(PAKLib::loadpak(path), "No sprites found");
	CHECK_THROWS_WITH(PAKLib::loadpak_fast(path), "Invalid sprite count");
	CHECK_THROWS_WITH(PAKLib::get_sprite(path, 0), "No sprites found");
}

TEST_CASE("Validation: a sprite count over 1500 is rejected") {
	fixture::pak_layout layout = fixture::build_pak(fixture::sprites(1));
	fixture::set_u32(layout.data, layout.count_offset, 1501);
	const std::string path = write_bad("too_many_sprites.pak", layout.data);
	CHECK_THROWS_WITH(PAKLib::loadpak(path), "Unreasonable number of sprites");
	CHECK_THROWS_WITH(PAKLib::loadpak_fast(path), "Invalid sprite count");
	CHECK_THROWS_WITH(PAKLib::get_sprite(path, 0), "Unreasonable number of sprites");
}

TEST_CASE("Validation: 1500 sprites is the largest accepted count") {
	const std::string path = write_bad("max_sprites.pak", fixture::build_pak(fixture::sprites(1500)).data);
	for (const fixture::full_loader& loader : fixture::full_loaders())
		CHECK(loader.load(path).sprites.size() == 1500);
	CHECK_NOTHROW(PAKLib::get_sprite(path, 1499));
}

TEST_CASE("Validation: a table shorter than the sprite count is rejected") {
	const fixture::pak_layout layout = fixture::build_pak(fixture::sprites(3));
	const std::string path = write_bad("short_table.pak", truncated(layout.data, layout.table_offsets[1] + 3));
	CHECK_THROWS_WITH(PAKLib::loadpak(path), "Failed to read sprite entries");
	CHECK_THROWS_WITH(PAKLib::loadpak_fast(path), "Failed to read sprite entries");
	CHECK_THROWS_WITH(PAKLib::get_sprite(path, 0), "Failed to read sprite entries");
}

TEST_CASE("Validation: a wrong sprite header magic is rejected") {
	std::vector<fixture::sprite_spec> specs(3);
	specs[1].valid_magic = false; // zeroed, as in MHelm3.pak / MHelm5.pak
	const std::string path = write_bad("bad_sprite_magic.pak", fixture::build_pak(specs).data);
	CHECK_THROWS_WITH(PAKLib::loadpak(path), "Invalid sprite header");
	CHECK_THROWS_WITH(PAKLib::get_sprite(path, 1), "Invalid sprite header");
	CHECK_NOTHROW(PAKLib::get_sprite(path, 0));
}

TEST_CASE("Validation: sprites must follow one another at their table offsets") {
	fixture::pak_layout layout = fixture::build_pak(fixture::sprites(2));
	fixture::set_u32(layout.data, layout.table_offsets[1], static_cast<uint32_t>(layout.sprite_offsets[1] + 1));
	const std::string path = write_bad("offset_mismatch.pak", layout.data);
	CHECK_THROWS_WITH(PAKLib::loadpak(path), "Sprite offset mismatch");
}

TEST_CASE("Validation: the first sprite must start right after the table") {
	fixture::pak_layout layout = fixture::build_pak(fixture::sprites(2));
	fixture::set_u32(layout.data, layout.table_offsets[0], static_cast<uint32_t>(layout.sprite_offsets[0] + 4));
	const std::string path = write_bad("first_offset_mismatch.pak", layout.data);
	CHECK_THROWS_WITH(PAKLib::loadpak(path), "Sprite offset mismatch");
}

TEST_CASE("Validation: a table size one byte short is caught at the next sprite") {
	fixture::pak_layout layout = fixture::build_pak(fixture::sprites(2));
	const uint32_t size = static_cast<uint32_t>(layout.sprite_offsets[1] - layout.sprite_offsets[0]);
	fixture::set_u32(layout.data, layout.table_offsets[0] + 4, size - 1);
	const std::string path = write_bad("short_size.pak", layout.data);
	CHECK_THROWS_WITH(PAKLib::loadpak(path), "Sprite offset mismatch");
}

TEST_CASE("Validation: a file cut inside the colour key is rejected") {
	const fixture::pak_layout layout = fixture::build_pak(fixture::sprites(1));
	const std::string path = write_bad("cut_in_key.pak", truncated(layout.data, layout.color_key_offsets[0] + 2));
	CHECK_THROWS_WITH(PAKLib::loadpak(path), "Failed to read image data");
	CHECK_THROWS_WITH(PAKLib::loadpak_fast(path), "Failed to read image data");
	CHECK_THROWS_WITH(PAKLib::get_sprite(path, 0), "Failed to read image data");
	CHECK_THROWS_WITH(PAKLib::get_sprite_fast(path, 0), "Failed to read image data");
}

TEST_CASE("Validation: a rect count of 0 is rejected") {
	fixture::pak_layout layout = fixture::build_pak(fixture::sprites(1));
	fixture::set_u32(layout.data, layout.rect_count_offsets[0], 0);
	const std::string path = write_bad("zero_rects.pak", layout.data);
	CHECK_THROWS_WITH(PAKLib::loadpak(path), "Unreasonable number of sprite rectangles");
	CHECK_THROWS_WITH(PAKLib::loadpak_fast(path), "Invalid rect count");
	CHECK_THROWS_WITH(PAKLib::get_sprite(path, 0), "Unreasonable number of sprite rectangles");
	CHECK_THROWS_WITH(PAKLib::get_sprite_fast(path, 0), "Invalid rect count");
}

TEST_CASE("Validation: a rect count over 1000 is rejected") {
	fixture::pak_layout layout = fixture::build_pak(fixture::sprites(1));
	fixture::set_u32(layout.data, layout.rect_count_offsets[0], 1001);
	const std::string path = write_bad("too_many_rects.pak", layout.data);
	CHECK_THROWS_WITH(PAKLib::loadpak(path), "Unreasonable number of sprite rectangles");
	CHECK_THROWS_WITH(PAKLib::loadpak_fast(path), "Invalid rect count");
	CHECK_THROWS_WITH(PAKLib::get_sprite(path, 0), "Unreasonable number of sprite rectangles");
	CHECK_THROWS_WITH(PAKLib::get_sprite_fast(path, 0), "Invalid rect count");
}

TEST_CASE("Validation: 1000 rects is the largest accepted count") {
	std::vector<fixture::sprite_spec> specs(1);
	specs[0].rects.assign(1000, fixture::rect{ 1, 1, 1, 1, 0, 0 });
	const std::string path = write_bad("max_rects.pak", fixture::build_pak(specs).data);
	for (const fixture::full_loader& loader : fixture::full_loaders())
		CHECK(loader.load(path).sprites[0].sprite_rectangles.size() == 1000);
	for (const fixture::sprite_loader& loader : fixture::sprite_loaders())
		CHECK(loader.load(path, 0).sprite_rectangles.size() == 1000);
}

TEST_CASE("Validation: a file cut inside the rect records is rejected") {
	std::vector<fixture::sprite_spec> specs(1);
	specs[0].rects.assign(5, fixture::rect{ 1, 1, 1, 1, 0, 0 });
	const fixture::pak_layout layout = fixture::build_pak(specs);
	const std::string path = write_bad("cut_in_rects.pak", truncated(layout.data, layout.rect_count_offsets[0] + 4 + 30));
	CHECK_THROWS_WITH(PAKLib::loadpak(path), "Failed to read sprite rectangles");
	CHECK_THROWS_WITH(PAKLib::loadpak_fast(path), "Failed to read sprite rectangles");
	CHECK_THROWS_WITH(PAKLib::get_sprite(path, 0), "Failed to read sprite rectangles");
	CHECK_THROWS_WITH(PAKLib::get_sprite_fast(path, 0), "Failed to read sprite rectangles");
}

TEST_CASE("Validation: a file cut inside the image data is rejected") {
	const fixture::pak_layout layout = fixture::build_pak(fixture::sprites(2));
	const std::string path = write_bad("cut_in_image.pak", truncated(layout.data, layout.data.size() - 10));
	CHECK_THROWS_WITH(PAKLib::loadpak(path), "Failed to read image data");
	CHECK_THROWS_WITH(PAKLib::loadpak_fast(path), "Failed to read image data");
	CHECK_THROWS_WITH(PAKLib::get_sprite(path, 1), "Failed to read image data");
	CHECK_THROWS_WITH(PAKLib::get_sprite_fast(path, 1), "Failed to read image data");
	CHECK_NOTHROW(PAKLib::get_sprite(path, 0));
}

TEST_CASE("Validation: get_sprite rejects an index past the sprite count") {
	const std::string path = write_bad("index_range.pak", fixture::build_pak(fixture::sprites(2)).data);
	CHECK_NOTHROW(PAKLib::get_sprite(path, 1));
	CHECK_THROWS_WITH(PAKLib::get_sprite(path, 2), "Sprite index out of bounds");
}
