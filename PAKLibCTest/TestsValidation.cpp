// Scenarios for malformed files. The strict readers (loadpak = PAK.ReadFromFile, pak::read = PAKData.Read) follow
// PAKLib 2.0 check for check and message for message; counts are uncapped but bounded by the data size.
// The fast readers (loadpak_fast, get_sprite_fast) skip the magic, image and sprite chain checks but follow the
// same count, bounds and index rules, with the same exceptions and messages.
#include "PakFixture.h"

namespace {
	using PAKLib::invalid_data_error;

	fixture::bytes truncated(fixture::bytes data, size_t keep) {
		data.resize(keep);
		return data;
	}

	std::string n(size_t v) { return std::to_string(v); }

	// Every strict reader throws invalid_data_error with exactly this message; so does get_sprite for sprite
	// get_sprite_index (pass -1 when get_sprite, which reads only its own sprite, does not see the fault)
	void check_strict_throws(const std::string& path, const std::string& message, int get_sprite_index = 0) {
		for (const fixture::full_loader& loader : fixture::strict_loaders())
			CHECK_THROWS_AS(loader.load(path), invalid_data_error, message);
		if (get_sprite_index >= 0)
			CHECK_THROWS_AS(PAKLib::get_sprite(path, static_cast<size_t>(get_sprite_index)), invalid_data_error, message);
	}

	// The fast readers throw the same invalid_data_error (get_sprite_fast for sprite get_sprite_index; -1 skips it)
	void check_fast_throws(const std::string& path, const std::string& message, int get_sprite_index = 0) {
		CHECK_THROWS_AS(PAKLib::loadpak_fast(path), invalid_data_error, message);
		if (get_sprite_index >= 0)
			CHECK_THROWS_AS(PAKLib::get_sprite_fast(path, static_cast<size_t>(get_sprite_index)), invalid_data_error, message);
	}

	void check_all_throws(const std::string& path, const std::string& message, int get_sprite_index = 0) {
		check_strict_throws(path, message, get_sprite_index);
		check_fast_throws(path, message, get_sprite_index);
	}

	// PAKData.Read's position check throws a plain Exception: std::runtime_error, never invalid_data_error
	void check_position_mismatch(const std::string& path, const std::string& message) {
		for (const fixture::full_loader& loader : fixture::strict_loaders())
			CHECK_THROWS_EXACT(loader.load(path), std::runtime_error, invalid_data_error, message);
	}
}

// ---- Paths (PAK.ReadFromFile) ----

TEST_CASE("Validation: an empty or blank path is an invalid argument") {
	CHECK_THROWS_AS(PAKLib::loadpak(""), std::invalid_argument, "File path cannot be null or empty.");
	CHECK_THROWS_AS(PAKLib::loadpak("  \t "), std::invalid_argument, "File path cannot be null or empty.");
	CHECK_THROWS_AS(PAKLib::get_sprite("", 0), std::invalid_argument, "File path cannot be null or empty.");
}

TEST_CASE("Validation: a missing file throws file_not_found_error, the fast readers their own message") {
	const std::string path = fixture::output_dir() + "/does_not_exist.pak";
	CHECK_THROWS_AS(PAKLib::loadpak(path), PAKLib::file_not_found_error, "The specified file does not exist.");
	try {
		PAKLib::loadpak(path);
	}
	catch (const PAKLib::file_not_found_error& ex) {
		CHECK(ex.file_name() == path);
	}
	CHECK_THROWS_AS(PAKLib::get_sprite(path, 0), PAKLib::file_not_found_error, "The specified file does not exist.");
	CHECK_THROWS_WITH(PAKLib::loadpak_fast(path), "Failed to open PAK file");
	CHECK_THROWS_WITH(PAKLib::get_sprite_fast(path, 0), "Failed to open PAK file");
}

TEST_CASE("Validation: .epak files are refused, in any case; a missing .epak is not found first") {
	const std::string message = "Encrypted .epak files are no longer supported (removed in PAKLib 2.0).";
	CHECK_THROWS_AS(PAKLib::loadpak(test::data_dir() + "/Amp.epak"), PAKLib::not_supported_error, message);
	const std::string upper = fixture::write("valid_but_named.EPAK", fixture::build_pak(fixture::sprites(1)).data);
	CHECK_THROWS_AS(PAKLib::loadpak(upper), PAKLib::not_supported_error, message);
	CHECK_THROWS_AS(PAKLib::get_sprite(upper, 0), PAKLib::not_supported_error, message);
	CHECK_THROWS_AS(PAKLib::loadpak(fixture::output_dir() + "/missing.epak"), PAKLib::file_not_found_error, "The specified file does not exist.");
	const std::string other = fixture::write("valid.epak.pak", fixture::build_pak(fixture::sprites(1)).data);
	CHECK(PAKLib::loadpak(other).sprites.size() == 1);
}

// ---- File header and sprite count ----

TEST_CASE("Validation: a wrong or short file header magic is rejected") {
	fixture::pak_layout layout = fixture::build_pak(fixture::sprites(1));
	layout.data[1] = 'Q';
	check_strict_throws(fixture::write("bad_file_magic.pak", layout.data), "Invalid PAK file header magic.");
	const std::string ten = fixture::write("ten_bytes.pak", truncated(layout.data, 10));
	check_strict_throws(ten, "Invalid PAK file header magic.");
	const std::string empty = fixture::write("empty.pak", fixture::bytes{});
	check_strict_throws(empty, "Invalid PAK file header magic.");
	// The fast readers skip the magic; a cut header then runs out at the sprite count
	CHECK(PAKLib::loadpak_fast(fixture::write("bad_file_magic.pak", layout.data)).sprites.size() == 1);
	CHECK_NOTHROW(PAKLib::get_sprite_fast(fixture::write("bad_file_magic.pak", layout.data), 0));
	check_fast_throws(ten, "Unexpected end of data at offset 10 while reading the sprite count.");
	check_fast_throws(empty, "Unexpected end of data at offset 0 while reading the sprite count.");
}

TEST_CASE("Validation: a file that ends before the sprite count is rejected before reading it") {
	const fixture::pak_layout layout = fixture::build_pak(fixture::sprites(1));
	check_all_throws(fixture::write("header_only.pak", truncated(layout.data, 20)),
		"Unexpected end of data at offset 20 while reading the sprite count.");
	check_all_throws(fixture::write("magic_only.pak", truncated(layout.data, 17)),
		"Unexpected end of data at offset 17 while reading the sprite count.");
}

TEST_CASE("Validation: a sprite count of 0 is accepted by every reader") {
	const std::string path = fixture::write("zero_sprites.pak", fixture::build_pak({}).data);
	for (const fixture::full_loader& loader : fixture::strict_loaders()) {
		const PAKLib::pak pak_file = loader.load(path);
		CHECK(pak_file.sprites.empty());
		CHECK(pak_file.sprite_count == 0);
	}
	const PAKLib::pak fast = PAKLib::loadpak_fast(path);
	CHECK(fast.sprites.empty());
	CHECK(fast.sprite_count == 0);
	CHECK_THROWS_AS(PAKLib::get_sprite_fast(path, 0), PAKLib::argument_out_of_range_error, "Sprite index out of bounds in PAK file: " + path);
}

TEST_CASE("Validation: sprite counts are uncapped in every reader (no 1500 limit)") {
	const std::string path = fixture::write("many_sprites.pak", fixture::build_pak(fixture::sprites(1501)).data);
	for (const fixture::full_loader& loader : fixture::full_loaders())
		CHECK(loader.load(path).sprites.size() == 1501);
	CHECK_NOTHROW(PAKLib::get_sprite_fast(path, 1500));
	CHECK_NOTHROW(PAKLib::get_sprite(path, 1500));
}

TEST_CASE("Validation: a negative or oversized sprite count is rejected before allocating") {
	fixture::pak_layout layout = fixture::build_pak(fixture::sprites(1));
	fixture::set_u32(layout.data, layout.count_offset, 0xFFFFFFFFu);
	check_all_throws(fixture::write("negative_count.pak", layout.data), "Sprite count -1 is negative.");
	fixture::set_u32(layout.data, layout.count_offset, 0x7FFFFFFFu);
	check_all_throws(fixture::write("huge_count.pak", layout.data),
		"Sprite count 2147483647 does not fit in the data: the sprite table would run past the end.");
}

TEST_CASE("Validation: a table shorter than the sprite count is rejected") {
	const fixture::pak_layout layout = fixture::build_pak(fixture::sprites(3));
	const std::string path = fixture::write("short_table.pak", truncated(layout.data, layout.table_offsets[1] + 3));
	check_all_throws(path, "Sprite count 3 does not fit in the data: the sprite table would run past the end.");
}

TEST_CASE("Validation: a negative table offset or length is rejected while reading the table") {
	fixture::pak_layout layout = fixture::build_pak(fixture::sprites(2));
	const size_t size1 = layout.data.size() - layout.sprite_offsets[1];
	fixture::set_u32(layout.data, layout.table_offsets[1], 0xFFFFFFFFu);
	check_all_throws(fixture::write("negative_offset.pak", layout.data),
		"Sprite table entry 1 has a negative offset (-1) or length (" + n(size1) + ").");
	layout = fixture::build_pak(fixture::sprites(2));
	fixture::set_u32(layout.data, layout.table_offsets[0] + 4, 0x80000000u);
	check_all_throws(fixture::write("negative_length.pak", layout.data),
		"Sprite table entry 0 has a negative offset (" + n(layout.sprite_offsets[0]) + ") or length (-2147483648).");
}

// ---- Sprite entries ----

TEST_CASE("Validation: a wrong sprite header magic is rejected with the sprite index and offset") {
	std::vector<fixture::sprite_spec> specs(3);
	specs[1].valid_magic = false; // zeroed, as in MHelm3.pak / MHelm5.pak
	const fixture::pak_layout layout = fixture::build_pak(specs);
	const std::string path = fixture::write("bad_sprite_magic.pak", layout.data);
	check_strict_throws(path, "Invalid sprite header magic for sprite 1 at offset " + n(layout.sprite_offsets[1]) + ".", 1);
	CHECK_NOTHROW(PAKLib::get_sprite(path, 2));
	CHECK(PAKLib::loadpak_fast(path).sprites.size() == 3);
	CHECK_NOTHROW(PAKLib::get_sprite_fast(path, 1));
	CHECK_NOTHROW(PAKLib::get_sprite(path, 0));
}

TEST_CASE("Validation: a file cut inside the sprite magic reports the magic") {
	const fixture::pak_layout layout = fixture::build_pak(fixture::sprites(1));
	const std::string cut_magic = fixture::write("cut_in_magic.pak", truncated(layout.data, layout.sprite_offsets[0] + 10));
	check_strict_throws(cut_magic, "Invalid sprite header magic for sprite 0 at offset " + n(layout.sprite_offsets[0]) + ".");
	check_fast_throws(cut_magic, "Unexpected end of data at offset " + n(layout.sprite_offsets[0] + 10) + " while reading the rectangle count.");
	check_all_throws(fixture::write("cut_in_padding.pak", truncated(layout.data, layout.sprite_offsets[0] + 50)),
		"Unexpected end of data at offset " + n(layout.sprite_offsets[0] + 50) + " while reading the rectangle count.");
}

TEST_CASE("Validation: sprites must follow one another at their table offsets") {
	fixture::pak_layout layout = fixture::build_pak(fixture::sprites(2));
	const size_t off1 = layout.sprite_offsets[1];
	fixture::set_u32(layout.data, layout.table_offsets[1], static_cast<uint32_t>(off1 + 1));
	const std::string path = fixture::write("offset_mismatch.pak", layout.data);
	check_position_mismatch(path, "Expected to be at position " + n(off1 + 1) + ", but was at " + n(off1) + ".");
	// get_sprite has no chain to check; it reads at the table offset and finds no magic there
	CHECK_THROWS_AS(PAKLib::get_sprite(path, 1), invalid_data_error, "Invalid sprite header magic for sprite 1 at offset " + n(off1 + 1) + ".");
	CHECK_NOTHROW(PAKLib::get_sprite(path, 0));
}

TEST_CASE("Validation: the first sprite must start right after the table") {
	fixture::pak_layout layout = fixture::build_pak(fixture::sprites(2));
	const size_t off0 = layout.sprite_offsets[0];
	fixture::set_u32(layout.data, layout.table_offsets[0], static_cast<uint32_t>(off0 + 4));
	const std::string path = fixture::write("first_offset_mismatch.pak", layout.data);
	check_position_mismatch(path, "Expected to be at position " + n(off0 + 4) + ", but was at " + n(off0) + ".");
	CHECK_THROWS_AS(PAKLib::get_sprite(path, 0), invalid_data_error, "Invalid sprite header magic for sprite 0 at offset " + n(off0 + 4) + ".");
}

TEST_CASE("Validation: a table size one byte short is caught by the image check or at the next sprite") {
	// BMP: the shortened image no longer holds its declared size, so it is not a valid BMP
	fixture::pak_layout layout = fixture::build_pak(fixture::sprites(2));
	uint32_t size = static_cast<uint32_t>(layout.sprite_offsets[1] - layout.sprite_offsets[0]);
	fixture::set_u32(layout.data, layout.table_offsets[0] + 4, size - 1);
	check_strict_throws(fixture::write("short_size_bmp.pak", layout.data), "Invalid sprite data format. Expected BMP or PNG.");

	// PNG: only the header is checked, so the next sprite is found one byte early
	std::vector<fixture::sprite_spec> specs(2);
	specs[0].image = fixture::make_png_header(2, 2);
	layout = fixture::build_pak(specs);
	size = static_cast<uint32_t>(layout.sprite_offsets[1] - layout.sprite_offsets[0]);
	fixture::set_u32(layout.data, layout.table_offsets[0] + 4, size - 1);
	const std::string png_path = fixture::write("short_size_png.pak", layout.data);
	check_position_mismatch(png_path,
		"Expected to be at position " + n(layout.sprite_offsets[1]) + ", but was at " + n(layout.sprite_offsets[1] - 1) + ".");
	CHECK(PAKLib::get_sprite(png_path, 0).image_data.size() == specs[0].image.size() - 1);
	CHECK_NOTHROW(PAKLib::get_sprite(png_path, 1));
}

TEST_CASE("Validation: a rect count of 0 is accepted by every reader") {
	std::vector<fixture::sprite_spec> specs(1);
	specs[0].rects.clear();
	const std::string path = fixture::write("zero_rects.pak", fixture::build_pak(specs).data);
	for (const fixture::full_loader& loader : fixture::strict_loaders()) {
		const PAKLib::pak pak_file = loader.load(path);
		REQUIRE(pak_file.sprites.size() == 1);
		CHECK(pak_file.sprites[0].sprite_rectangles.empty());
		CHECK(pak_file.sprites[0].image_data == specs[0].image);
	}
	const PAKLib::pak fast = PAKLib::loadpak_fast(path);
	REQUIRE(fast.sprites.size() == 1);
	CHECK(fast.sprites[0].sprite_rectangles.empty());
	CHECK(fast.sprites[0].image_data == specs[0].image);
	for (const fixture::sprite_loader& loader : fixture::sprite_loaders())
		CHECK(loader.load(path, 0).image_data == specs[0].image);
}

TEST_CASE("Validation: rect counts are uncapped in every reader (no 1000 limit)") {
	std::vector<fixture::sprite_spec> specs(1);
	specs[0].rects.assign(1001, fixture::rect{ 1, 1, 1, 1, 0, 0 });
	const std::string path = fixture::write("many_rects.pak", fixture::build_pak(specs).data);
	for (const fixture::full_loader& loader : fixture::full_loaders())
		CHECK(loader.load(path).sprites[0].sprite_rectangles.size() == 1001);
	for (const fixture::sprite_loader& loader : fixture::sprite_loaders())
		CHECK(loader.load(path, 0).sprite_rectangles.size() == 1001);
}

TEST_CASE("Validation: a negative or oversized rect count is rejected before allocating") {
	fixture::pak_layout layout = fixture::build_pak(fixture::sprites(1));
	fixture::set_u32(layout.data, layout.rect_count_offsets[0], 0x80000000u);
	check_all_throws(fixture::write("negative_rects.pak", layout.data), "Sprite 0: rectangle count -2147483648 is negative.");
	fixture::set_u32(layout.data, layout.rect_count_offsets[0], 0x7FFFFFFFu);
	check_all_throws(fixture::write("huge_rects.pak", layout.data), "Sprite 0: rectangle count 2147483647 does not fit in the data.");
}

TEST_CASE("Validation: a file cut inside the rect records is rejected") {
	std::vector<fixture::sprite_spec> specs(1);
	specs[0].rects.assign(5, fixture::rect{ 1, 1, 1, 1, 0, 0 });
	const fixture::pak_layout layout = fixture::build_pak(specs);
	const std::string path = fixture::write("cut_in_rects.pak", truncated(layout.data, layout.rect_count_offsets[0] + 4 + 30));
	check_all_throws(path, "Sprite 0: rectangle count 5 does not fit in the data.");
}

TEST_CASE("Validation: a file cut inside the colour key is rejected") {
	const fixture::pak_layout layout = fixture::build_pak(fixture::sprites(1));
	const std::string path = fixture::write("cut_in_key.pak", truncated(layout.data, layout.color_key_offsets[0] + 2));
	check_all_throws(path, "Unexpected end of data at offset " + n(layout.color_key_offsets[0]) + " while reading the color key.");
}

TEST_CASE("Validation: a file cut inside the image data is rejected") {
	const fixture::pak_layout layout = fixture::build_pak(fixture::sprites(2));
	const std::string path = fixture::write("cut_in_image.pak", truncated(layout.data, layout.data.size() - 10));
	check_all_throws(path, "Sprite 1: image length 58 runs past the end of the data.", 1);
	CHECK_NOTHROW(PAKLib::get_sprite(path, 0));
	CHECK_NOTHROW(PAKLib::get_sprite_fast(path, 0));
}

TEST_CASE("Validation: a table size below the fixed part or past the file never allocates") {
	fixture::pak_layout layout = fixture::build_pak(fixture::sprites(1));
	fixture::set_u32(layout.data, layout.table_offsets[0] + 4, 50);
	std::string path = fixture::write("size_below_fixed.pak", layout.data);
	check_all_throws(path, "Sprite 0: image length -70 is negative.");

	fixture::set_u32(layout.data, layout.table_offsets[0] + 4, 0x7FFFFFF0u);
	path = fixture::write("size_past_file.pak", layout.data);
	check_all_throws(path, "Sprite 0: image length " + n(0x7FFFFFF0u - 120) + " runs past the end of the data.");

	// Sizes that only fit as unsigned values are negative int32 table entries for every reader
	fixture::set_u32(layout.data, layout.table_offsets[0] + 4, 0xFFFFFFF0u);
	path = fixture::write("size_unsigned_huge.pak", layout.data);
	check_all_throws(path, "Sprite table entry 0 has a negative offset (" + n(layout.sprite_offsets[0]) + ") or length (-16).");
}

TEST_CASE("Validation: get_sprite_fast checks the index against the sprite count, as get_sprite does") {
	const std::string path = fixture::write("index_range.pak", fixture::build_pak(fixture::sprites(2)).data);
	CHECK_NOTHROW(PAKLib::get_sprite_fast(path, 1));
	CHECK_THROWS_AS(PAKLib::get_sprite_fast(path, 2), PAKLib::argument_out_of_range_error, "Sprite index out of bounds in PAK file: " + path);
	CHECK_THROWS_AS(PAKLib::get_sprite_fast(path, 100000), PAKLib::argument_out_of_range_error, "Sprite index out of bounds in PAK file: " + path);
}

TEST_CASE("Validation: trailing bytes after the last sprite are ignored, as in PAKLib") {
	fixture::bytes data = fixture::build_pak(fixture::sprites(2)).data;
	data.insert(data.end(), 33, 0xEE);
	const std::string path = fixture::write("trailing.pak", data);
	for (const fixture::full_loader& loader : fixture::strict_loaders())
		CHECK(loader.load(path).sprites.size() == 2);
}

// ---- Image checks (IsValidBmp / IsValidPng, then the 4096 cap) ----

TEST_CASE("Validation: image data must be a BMP or a PNG") {
	struct bad_image { const char* name; fixture::bytes image; };
	std::vector<bad_image> cases;
	cases.push_back({ "text", fixture::bytes{ 'h', 'e', 'l', 'l', 'o', ' ', 'w', 'o', 'r', 'l', 'd', '!', '!', '!', '!', '!', '!', '!', '!', '!', '!', '!', '!', '!', '!', '!', '!', '!' } });
	cases.push_back({ "empty", fixture::bytes{} });
	fixture::bytes bmp20(20, 0);
	bmp20[0] = 'B';
	bmp20[1] = 'M';
	cases.push_back({ "bmp_20_bytes", bmp20 });   // PAKLib's 14-25 byte quirk is not ported: 26 bytes are required
	fixture::bytes oversized = fixture::make_bmp(2, 2);
	fixture::set_u32(oversized, 2, static_cast<uint32_t>(oversized.size() + 1));
	cases.push_back({ "bmp_declared_size_too_big", oversized });
	fixture::bytes zero_width = fixture::make_bmp(2, 2);
	fixture::set_u32(zero_width, 18, 0);
	cases.push_back({ "bmp_zero_width", zero_width });
	fixture::bytes top_down = fixture::make_bmp(2, 2);
	fixture::set_u32(top_down, 22, static_cast<uint32_t>(-2));
	cases.push_back({ "bmp_top_down", top_down });
	fixture::bytes ihdr12 = fixture::make_png_header(2, 2);
	ihdr12[11] = 12;
	cases.push_back({ "png_ihdr_length_12", ihdr12 });
	fixture::bytes not_ihdr = fixture::make_png_header(2, 2);
	not_ihdr[15] = 'X';
	cases.push_back({ "png_first_chunk_not_ihdr", not_ihdr });
	cases.push_back({ "png_23_bytes", truncated(fixture::make_png_header(2, 2), 23) });

	for (const bad_image& c : cases) {
		std::vector<fixture::sprite_spec> specs(2);
		specs[1].image = c.image;
		const std::string path = fixture::write(std::string("bad_image_") + c.name + ".pak", fixture::build_pak(specs).data);
		check_strict_throws(path, "Invalid sprite data format. Expected BMP or PNG.", 1);
		CHECK_NOTHROW(PAKLib::get_sprite(path, 0));
		CHECK(PAKLib::loadpak_fast(path).sprites.size() == 2);
	}
}

TEST_CASE("Validation: a negative BMP declared size still passes, as in PAKLib") {
	std::vector<fixture::sprite_spec> specs(1);
	fixture::set_u32(specs[0].image, 2, 0xFFFFFFFFu);
	const std::string path = fixture::write("bmp_negative_declared.pak", fixture::build_pak(specs).data);
	for (const fixture::full_loader& loader : fixture::strict_loaders())
		CHECK(loader.load(path).sprites.size() == 1);
}

TEST_CASE("Validation: images over 4096 on a side are rejected with the sprite index") {
	std::vector<fixture::sprite_spec> specs(2);
	specs[1].image = fixture::make_png_header(4097, 1);
	check_strict_throws(fixture::write("png_too_wide.pak", fixture::build_pak(specs).data),
		"Sprite 1: Image is 4097x1; the limit is 4096x4096.", 1);
	specs[1].image = fixture::make_png_header(5, 9000);
	check_strict_throws(fixture::write("png_too_tall.pak", fixture::build_pak(specs).data),
		"Sprite 1: Image is 5x9000; the limit is 4096x4096.", 1);
	specs[0].image = fixture::make_bmp(1, 4097);
	specs[1].image = fixture::make_bmp(1, 1);
	check_strict_throws(fixture::write("bmp_too_tall.pak", fixture::build_pak(specs).data),
		"Sprite 0: Image is 1x4097; the limit is 4096x4096.");
	CHECK(PAKLib::loadpak_fast(fixture::write("bmp_too_tall.pak", fixture::build_pak(specs).data)).sprites.size() == 2);
}

TEST_CASE("Validation: 4096x4096 is the largest image accepted") {
	std::vector<fixture::sprite_spec> specs(2);
	specs[0].image = fixture::make_png_header(4096, 4096);
	specs[1].image = fixture::make_png_header(0x80000000u, 1);  // negative width is not over the limit, as in PAKLib
	const std::string path = fixture::write("png_max.pak", fixture::build_pak(specs).data);
	for (const fixture::full_loader& loader : fixture::strict_loaders())
		CHECK(loader.load(path).sprites.size() == 2);
}

TEST_CASE("Validation: get_sprite rejects an index past the sprite count") {
	const std::string path = fixture::write("index_range_strict.pak", fixture::build_pak(fixture::sprites(2)).data);
	CHECK_NOTHROW(PAKLib::get_sprite(path, 1));
	CHECK_THROWS_AS(PAKLib::get_sprite(path, 2), PAKLib::argument_out_of_range_error, "Sprite index out of bounds in PAK file: " + path);
	const std::string empty = fixture::write("index_range_empty.pak", fixture::build_pak({}).data);
	CHECK_THROWS_AS(PAKLib::get_sprite(empty, 0), PAKLib::argument_out_of_range_error, "Sprite index out of bounds in PAK file: " + empty);
}

TEST_CASE("Validation: pak::read, loadpak and get_sprite agree") {
	std::vector<fixture::sprite_spec> specs(3);
	specs[1].image = fixture::make_png_header(3, 3);
	specs[2].color_key[0] = 0xFF;
	specs[2].color_key[3] = 0x10;
	const fixture::pak_layout layout = fixture::build_pak(specs);
	const std::string path = fixture::write("read_agree.pak", layout.data);
	const PAKLib::pak from_file = PAKLib::loadpak(path);
	const PAKLib::pak from_bytes = PAKLib::pak::read(layout.data);
	REQUIRE(from_file.sprites.size() == from_bytes.sprites.size());
	for (size_t i = 0; i < from_file.sprites.size(); ++i)
		CHECK(fixture::same_sprite(from_file.sprites[i], from_bytes.sprites[i]));
	for (size_t i = 0; i < from_file.sprites.size(); ++i)
		CHECK(fixture::same_sprite(PAKLib::get_sprite(path, i), from_bytes.sprites[i]));
	CHECK(from_bytes.sprites[2].get_color_key() == 0xFF000010u);
	REQUIRE(from_bytes.sprite_entries.size() == 3);
	CHECK(from_bytes.sprite_entries[1].offset == layout.sprite_offsets[1]);
}

TEST_CASE("Validation: byte counts are checked against size_t before allocating (32-bit builds)") {
	// A file source can be larger than a 32-bit address space; such counts must not wrap when allocated
	CHECK(PAKLib::detail::fits_size_t(0));
	CHECK(PAKLib::detail::fits_size_t(0x7FFFFFFF));
	CHECK(PAKLib::detail::fits_size_t(0xFFFFFFFFLL));
	CHECK(PAKLib::detail::fits_size_t(0x100000000LL) == (sizeof(size_t) > 4));
	CHECK(PAKLib::detail::fits_size_t(static_cast<int64_t>(0x7FFFFFFF) * 12) == (sizeof(size_t) > 4));
}
