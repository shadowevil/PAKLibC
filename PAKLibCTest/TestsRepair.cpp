// Scenarios for repair (PAKRepair.Repair, PAK.RepairFile, PAKRepairReport): each damage kind, with and without
// force, checked against the exact issue list PAKLib produces.
#include "PakFixture.h"
#include <algorithm>
#include <cstring>

namespace {
	using fixture::bytes;
	namespace pr = PAKLib::pak_repair;

	std::string n(long long v) { return std::to_string(v); }

	std::vector<std::string> issue_strings(const PAKLib::repair_report& report) {
		std::vector<std::string> out;
		for (const PAKLib::repair_issue& issue : report.issues)
			out.push_back(issue.to_string());
		return out;
	}

	void check_issues(const PAKLib::repair_report& report, const std::vector<std::string>& expected) {
		const std::vector<std::string> actual = issue_strings(report);
		CHECK(actual == expected);
		if (actual != expected) {
			for (const std::string& s : actual)
				std::printf("    actual:   %s\n", s.c_str());
			for (const std::string& s : expected)
				std::printf("    expected: %s\n", s.c_str());
		}
	}

	// The repaired data writes and reads back cleanly
	void check_rebuilds(const PAKLib::repair_report& report) {
		const bytes out = report.data.to_bytes();
		const PAKLib::pak back = PAKLib::pak::read(out);
		REQUIRE(back.sprites.size() == report.data.sprites.size());
		for (size_t i = 0; i < back.sprites.size(); ++i)
			CHECK(fixture::same_sprite(back.sprites[i], report.data.sprites[i]));
	}

	std::vector<fixture::sprite_spec> three_sprites() {
		std::vector<fixture::sprite_spec> specs(3);
		specs[0].image = fixture::make_bmp(2, 2, 1, 2, 3);
		specs[1].image = fixture::make_png_header(3, 3);
		specs[1].rects.assign(3, fixture::rect{ 5, 6, 7, 8, -1, 1 });
		specs[1].color_key[0] = 0xFF;
		specs[1].color_key[3] = 0x40;
		specs[2].image = fixture::make_bmp(1, 1, 9, 9, 9);
		return specs;
	}

	// Adds a table entry (count + 1), shifting every sprite and existing table offset by 8
	void add_table_entry(fixture::pak_layout& layout, uint32_t offset, uint32_t length) {
		bytes& d = layout.data;
		const uint32_t count = d[20] | (d[21] << 8) | (d[22] << 16) | (static_cast<uint32_t>(d[23]) << 24);
		for (uint32_t i = 0; i < count; ++i) {
			const size_t at = 24 + i * 8;
			const uint32_t old = d[at] | (d[at + 1] << 8) | (d[at + 2] << 16) | (static_cast<uint32_t>(d[at + 3]) << 24);
			fixture::set_u32(d, at, old + 8);
		}
		bytes entry;
		fixture::put_u32(entry, offset);
		fixture::put_u32(entry, length);
		d.insert(d.begin() + 24 + count * 8, entry.begin(), entry.end());
		fixture::set_u32(d, 20, count + 1);
		for (size_t& o : layout.sprite_offsets)
			o += 8;
		for (size_t& o : layout.color_key_offsets)
			o += 8;
		for (size_t& o : layout.rect_count_offsets)
			o += 8;
	}
}

TEST_CASE("Repair: an undamaged file has no issues and rebuilds byte for byte") {
	const fixture::pak_layout layout = fixture::build_pak(three_sprites());
	for (int force = 0; force < 2; ++force) {
		const PAKLib::repair_report report = pr::repair(layout.data, force != 0);
		CHECK(report.issues.empty());
		CHECK(report.success());
		CHECK(report.placeholder_sprites.empty());
		CHECK(report.data.to_bytes() == layout.data);
	}
	const bytes amp = fixture::read(fixture::amp_pak());
	const PAKLib::repair_report amp_report = pr::repair(amp);
	CHECK(amp_report.issues.empty());
	CHECK(amp_report.data.sprites.size() == 40);
	CHECK(amp_report.data.to_bytes() == amp);
}

TEST_CASE("Repair: report and issue shape") {
	PAKLib::repair_issue file_issue;
	file_issue.offset = 7;
	file_issue.description = "x";
	CHECK(file_issue.sprite_index == -1);
	CHECK(file_issue.to_string() == "[not repaired] file @ 7: x");
	PAKLib::repair_issue sprite_issue;
	sprite_issue.sprite_index = 3;
	sprite_issue.offset = 9000000000LL;
	sprite_issue.description = "y";
	sprite_issue.repaired = true;
	CHECK(sprite_issue.to_string() == "[repaired] sprite 3 @ 9000000000: y");
	PAKLib::repair_report report;
	CHECK(report.success());
	report.issues.push_back(sprite_issue);
	CHECK(report.success());
	report.issues.push_back(file_issue);
	CHECK(!report.success());
}

TEST_CASE("Repair: a truncated file drops the cut sprite, or replaces it with force") {
	const fixture::pak_layout layout = fixture::build_pak(three_sprites());
	const bytes cut(layout.data.begin(), layout.data.end() - 20);
	const std::string where = n(static_cast<long long>(layout.sprite_offsets[2]));

	const PAKLib::repair_report dropped = pr::repair(cut);
	check_issues(dropped, { "[not repaired] sprite 2 @ " + where + ": Image data is incomplete; dropped (repair with force to insert a placeholder)." });
	CHECK(!dropped.success());
	CHECK(dropped.data.sprites.size() == 2);
	CHECK(dropped.placeholder_sprites.empty());
	check_rebuilds(dropped);

	const PAKLib::repair_report forced = pr::repair(cut, true);
	check_issues(forced, { "[repaired] sprite 2 @ " + where + ": Image data is incomplete; replaced with a 32x32 placeholder." });
	CHECK(forced.success());
	REQUIRE(forced.data.sprites.size() == 3);
	CHECK((forced.placeholder_sprites == std::vector<int>{ 2 }));
	const PAKLib::sprite& placeholder = forced.data.sprites[2];
	REQUIRE(placeholder.sprite_rectangles.size() == 1);
	CHECK(placeholder.sprite_rectangles[0].width == 32);
	CHECK(placeholder.sprite_rectangles[0].height == 32);
	CHECK(placeholder.sprite_rectangles[0].x == 0);
	CHECK(!placeholder.has_color_key());
	CHECK(placeholder.image_data.size() == 54 + 32 * 32 * 3);
	CHECK(PAKLib::sprite_image::get_bmp_bits_per_pixel(placeholder.image_data) == 24);
	const PAKLib::detail::image img = PAKLib::detail::decode(placeholder.image_data);
	// Magenta where (x / 8 + row / 8) is even, counting rows as stored (bottom-up)
	CHECK(fixture::rgba_at(img, 0, 31) == 0xFF00FFFFu);
	CHECK(fixture::rgba_at(img, 8, 31) == 0x000000FFu);
	CHECK(fixture::rgba_at(img, 0, 0) == 0x000000FFu);
	CHECK(fixture::rgba_at(img, 8, 0) == 0xFF00FFFFu);
	check_rebuilds(forced);
	CHECK(fixture::same_sprite(forced.data.sprites[0], PAKLib::pak::read(layout.data).sprites[0]));
}

TEST_CASE("Repair: a shuffled sprite table rebuilds the sprites in file order") {
	const fixture::pak_layout layout = fixture::build_pak(three_sprites());
	bytes shuffled = layout.data;
	std::swap_ranges(shuffled.begin() + 24, shuffled.begin() + 32, shuffled.begin() + 32);
	CHECK_THROWS_WITH(PAKLib::pak::read(shuffled), "Expected to be at position");
	const PAKLib::repair_report report = pr::repair(shuffled);
	CHECK(report.issues.empty());
	CHECK(report.data.to_bytes() == layout.data);

	// Offsets swapped but lengths left: each length is flagged at its table entry
	bytes offsets_only = layout.data;
	std::swap_ranges(offsets_only.begin() + 24, offsets_only.begin() + 28, offsets_only.begin() + 32);
	const long long len0 = static_cast<long long>(layout.sprite_offsets[1] - layout.sprite_offsets[0]);
	const long long len1 = static_cast<long long>(layout.sprite_offsets[2] - layout.sprite_offsets[1]);
	fixture::write("repair_shuffled.pak", shuffled);
	fixture::write("repair_offsets_only.pak", offsets_only);
	// Offsets swapped, lengths left: sprite 1 now has sprite 0's table length, one byte longer than itself, so
	// repair keeps that byte (the first byte of sprite 2's header), as PAKLib does; the knock-on effect loses
	// sprite 2. Expected text checked against PAKLib 2.0's own output (C# cross-check).
	REQUIRE(len0 == len1 + 1);
	const PAKLib::repair_report lengths = pr::repair(offsets_only);
	const std::string where = n(static_cast<long long>(layout.color_key_offsets[2] + 4));   // sprite 2's image
	check_issues(lengths, {
		"[repaired] sprite 0 @ 36: Sprite table length was " + n(len1) + ", sprite is " + n(len0) + " bytes; rebuilt.",
		"[repaired] sprite 2 @ 40: Sprite table offset was " + n(static_cast<long long>(layout.sprite_offsets[2])) + ", sprite found at " + where + "; rebuilt.",
		"[not repaired] sprite 2 @ " + where + ": Image data found at " + where + " but its sprite header and frames could not be recovered; dropped (repair with force to insert a placeholder).",
	});
	CHECK(!lengths.success());
	CHECK(lengths.data.sprites.size() == 2);
}

TEST_CASE("Repair: zeroed sprite magic (MHelm3.pak style) is rebuilt") {
	std::vector<fixture::sprite_spec> specs = three_sprites();
	specs[1].valid_magic = false;
	const fixture::pak_layout layout = fixture::build_pak(specs);
	CHECK_THROWS_WITH(PAKLib::pak::read(layout.data), "Invalid sprite header magic for sprite 1");
	const PAKLib::repair_report report = pr::repair(layout.data);
	check_issues(report, { "[repaired] sprite 1 @ " + n(static_cast<long long>(layout.sprite_offsets[1])) + ": Sprite header magic was invalid; rebuilt." });
	CHECK(report.success());
	CHECK(report.data.to_bytes() == fixture::build_pak(three_sprites()).data);
}

TEST_CASE("Repair: zeroed file magic is rebuilt") {
	fixture::pak_layout layout = fixture::build_pak(three_sprites());
	std::fill(layout.data.begin(), layout.data.begin() + 17, static_cast<uint8_t>(0));
	const PAKLib::repair_report report = pr::repair(layout.data);
	check_issues(report, { "[repaired] file @ 0: File header magic was invalid; rebuilt." });
	CHECK(report.data.to_bytes() == fixture::build_pak(three_sprites()).data);
}

TEST_CASE("Repair: an impossible sprite count rebuilds the table from the sprites found") {
	fixture::pak_layout layout = fixture::build_pak(three_sprites());
	fixture::set_u32(layout.data, 20, 0x7FFFFFFFu);
	const PAKLib::repair_report report = pr::repair(layout.data);
	check_issues(report, { "[repaired] file @ 20: Sprite count 2147483647 does not fit in the file; table rebuilt from the sprites found." });
	CHECK(report.data.to_bytes() == fixture::build_pak(three_sprites()).data);

	fixture::set_u32(layout.data, 20, 0xFFFFFFFFu);
	check_issues(pr::repair(layout.data), { "[repaired] file @ 20: Sprite count -1 does not fit in the file; table rebuilt from the sprites found." });
}

TEST_CASE("Repair: a sprite count of 0 with sprites present is reported") {
	fixture::pak_layout layout = fixture::build_pak(three_sprites());
	fixture::set_u32(layout.data, 20, 0);
	const PAKLib::repair_report report = pr::repair(layout.data);
	check_issues(report, { "[repaired] file @ 20: Sprite count was 0 but 3 sprites were found; table rebuilt." });
	CHECK(report.data.sprites.size() == 3);
}

TEST_CASE("Repair: a damaged rectangle count is recovered from the frames") {
	fixture::pak_layout layout = fixture::build_pak(three_sprites());
	fixture::set_u32(layout.data, layout.rect_count_offsets[1], 999);
	const PAKLib::repair_report report = pr::repair(layout.data);
	check_issues(report, { "[repaired] sprite 1 @ " + n(static_cast<long long>(layout.rect_count_offsets[1])) + ": Rectangle count was 999, 3 frames found; rebuilt." });
	CHECK(report.data.to_bytes() == fixture::build_pak(three_sprites()).data);
}

TEST_CASE("Repair: a BMP size field past the data is corrected from the pixel layout") {
	fixture::pak_layout layout = fixture::build_pak(three_sprites());
	const size_t image = layout.color_key_offsets[0] + 4;
	fixture::set_u32(layout.data, image + 2, 0x7FFFFFFFu);
	const PAKLib::repair_report report = pr::repair(layout.data);
	check_issues(report, { "[repaired] sprite 0 @ " + n(static_cast<long long>(image + 2)) + ": BMP size field was 2147483647, image is 70 bytes; rebuilt." });
	CHECK(report.data.to_bytes() == fixture::build_pak(three_sprites()).data);
}

TEST_CASE("Repair: a sprite whose image is destroyed is dropped, or replaced keeping its frame count") {
	fixture::pak_layout layout = fixture::build_pak(three_sprites());
	const size_t image = layout.color_key_offsets[1] + 4;
	std::fill(layout.data.begin() + static_cast<std::ptrdiff_t>(image), layout.data.begin() + static_cast<std::ptrdiff_t>(image + 8), static_cast<uint8_t>(0));
	const std::string where = n(static_cast<long long>(layout.sprite_offsets[1]));

	const PAKLib::repair_report dropped = pr::repair(layout.data);
	check_issues(dropped, { "[not repaired] sprite 1 @ " + where + ": Sprite header found but its image data is missing or damaged; dropped (repair with force to insert a placeholder)." });
	CHECK(dropped.data.sprites.size() == 2);

	const PAKLib::repair_report forced = pr::repair(layout.data, true);
	check_issues(forced, { "[repaired] sprite 1 @ " + where + ": Sprite header found but its image data is missing or damaged; replaced with a 32x32 placeholder." });
	REQUIRE(forced.data.sprites.size() == 3);
	CHECK(forced.data.sprites[1].sprite_rectangles.size() == 3);
	CHECK((forced.placeholder_sprites == std::vector<int>{ 1 }));
	check_rebuilds(forced);
}

TEST_CASE("Repair: 16-byte rectangle records are flagged as unsupported") {
	bytes d;
	fixture::put_text(d, "<Pak file header>", 17);
	d.insert(d.end(), 3, 0);
	fixture::put_u32(d, 1);
	fixture::put_u32(d, 32);
	fixture::put_u32(d, 0);   // length filled below
	fixture::put_text(d, "<Sprite File Header>", 20);
	d.insert(d.end(), 80, 0);
	fixture::put_u32(d, 2);
	d.insert(d.end(), 32, 0x41);
	d.insert(d.end(), 4, 0);
	const bytes image = fixture::make_bmp(1, 1);
	d.insert(d.end(), image.begin(), image.end());
	fixture::set_u32(d, 28, static_cast<uint32_t>(d.size() - 32));

	check_issues(pr::repair(d), { "[not repaired] sprite 0 @ 32: Uses 16-byte rectangle records, which are not supported; dropped (repair with force to insert a placeholder)." });
	const PAKLib::repair_report forced = pr::repair(d, true);
	check_issues(forced, { "[repaired] sprite 0 @ 32: Uses 16-byte rectangle records, which are not supported; replaced with a 32x32 placeholder." });
	REQUIRE(forced.data.sprites.size() == 1);
	CHECK(forced.data.sprites[0].sprite_rectangles.size() == 2);
}

TEST_CASE("Repair: images that cannot be decoded or are over 4096 are corrupt sprites") {
	std::vector<fixture::sprite_spec> specs = three_sprites();
	fixture::bmp_spec s;
	s.width = 2;
	s.height = 2;
	s.bpp = 8;
	s.compression = 4;
	s.palette = { 0, 0xFFFFFF };
	specs[0].image = fixture::make_bmp_custom(s, bytes(8, 0));
	specs[1].image = fixture::make_png_header(5000, 1);
	const fixture::pak_layout layout = fixture::build_pak(specs);
	check_issues(pr::repair(layout.data), {
		"[not repaired] sprite 0 @ " + n(static_cast<long long>(layout.sprite_offsets[0])) + ": Image data cannot be decoded (Unsupported BMP compression 4.); dropped (repair with force to insert a placeholder).",
		"[not repaired] sprite 1 @ " + n(static_cast<long long>(layout.sprite_offsets[1])) + ": Image data cannot be decoded (Image is 5000x1; the limit is 4096x4096.); dropped (repair with force to insert a placeholder).",
	});
	const PAKLib::repair_report forced = pr::repair(layout.data, true);
	CHECK((forced.placeholder_sprites == std::vector<int>{ 0, 1 }));
	CHECK(forced.data.sprites[1].sprite_rectangles.size() == 3);
	check_rebuilds(forced);
}

TEST_CASE("Repair: trailing bytes are dropped and reported") {
	fixture::pak_layout layout = fixture::build_pak(three_sprites());
	const size_t end = layout.data.size();
	layout.data.insert(layout.data.end(), 33, 0xEE);
	const PAKLib::repair_report report = pr::repair(layout.data);
	check_issues(report, { "[repaired] file @ " + n(static_cast<long long>(end)) + ": 33 trailing bytes after the last sprite; dropped." });
	CHECK(report.success());
}

TEST_CASE("Repair: a table entry pointing at unclaimed garbage is a lost sprite") {
	fixture::pak_layout layout = fixture::build_pak(three_sprites());
	layout.data.insert(layout.data.end(), 200, 0x11);
	const uint32_t garbage = static_cast<uint32_t>(layout.data.size() - 200 + 8);
	add_table_entry(layout, garbage, 200);
	const std::string where = n(garbage);
	const PAKLib::repair_report dropped = pr::repair(layout.data);
	check_issues(dropped, {
		"[not repaired] sprite 3 @ " + where + ": Listed in the sprite table (offset " + where + ", length 200) but no sprite could be recovered there; dropped (repair with force to insert a placeholder).",
		"[repaired] file @ " + where + ": 200 trailing bytes after the last sprite; dropped.",
	});
	const PAKLib::repair_report forced = pr::repair(layout.data, true);
	REQUIRE(forced.data.sprites.size() == 4);
	CHECK(forced.data.sprites[3].sprite_rectangles.size() == 1);
	CHECK((forced.placeholder_sprites == std::vector<int>{ 3 }));
	CHECK(forced.success());
}

TEST_CASE("Repair: a table entry that matches nothing is reported, not repaired") {
	fixture::pak_layout layout = fixture::build_pak(three_sprites());
	add_table_entry(layout, 0xFFFFFFFFu, 5);
	const PAKLib::repair_report report = pr::repair(layout.data);
	check_issues(report, { "[not repaired] file @ 48: Sprite table entry 3 (offset -1, length 5) matches no recoverable sprite." });
	CHECK(!report.success());
	CHECK(report.data.sprites.size() == 3);
}

TEST_CASE("Repair: a sprite missing from the table is added") {
	fixture::pak_layout layout = fixture::build_pak(three_sprites());
	fixture::set_u32(layout.data, 20, 2);   // the third table entry now reads as the gap before sprite 0
	fixture::write("repair_missing_from_table.pak", layout.data);
	const PAKLib::repair_report report = pr::repair(layout.data);
	check_issues(report, { "[repaired] sprite 2 @ " + n(static_cast<long long>(layout.sprite_offsets[2])) + ": Sprite was not listed in the sprite table; added." });
	REQUIRE(report.data.sprites.size() == 3);
	CHECK(report.data.to_bytes() == fixture::build_pak(three_sprites()).data);
}

TEST_CASE("Repair: tiny and empty inputs") {
	check_issues(pr::repair(bytes(10, 0)), {
		"[not repaired] file @ 0: File is 10 bytes, too short for a PAK header.",
		"[not repaired] file @ 0: No sprites could be found.",
	});
	check_issues(pr::repair(bytes()), { "[not repaired] file @ 0: File is 0 bytes, too short for a PAK header." });
	// A valid but empty PAK: PAKLib reports that nothing was found (slots.Count == 0 && bytes.Length > 0)
	fixture::write("repair_empty_pak.pak", fixture::build_pak({}).data);
	check_issues(pr::repair(fixture::build_pak({}).data), { "[not repaired] file @ 0: No sprites could be found." });
}

TEST_CASE("Repair: a section salvages only its own sprites and checks its bounds") {
	const fixture::pak_layout layout = fixture::build_pak(three_sprites());
	const int64_t size = static_cast<int64_t>(layout.data.size());
	const PAKLib::repair_report section = pr::repair(layout.data, static_cast<int64_t>(layout.sprite_offsets[1]), size);
	CHECK(section.issues.empty());
	REQUIRE(section.data.sprites.size() == 2);
	const PAKLib::pak whole = PAKLib::pak::read(layout.data);
	CHECK(fixture::same_sprite(section.data.sprites[0], whole.sprites[1]));
	CHECK(fixture::same_sprite(section.data.sprites[1], whole.sprites[2]));

	CHECK_THROWS_AS(pr::repair(layout.data, -1, size), PAKLib::argument_out_of_range_error, "Section must lie within the data.");
	CHECK_THROWS_AS(pr::repair(layout.data, 0, size + 1), PAKLib::argument_out_of_range_error, "Section must lie within the data.");
	CHECK_THROWS_AS(pr::repair(layout.data, 50, 49), std::invalid_argument, "Section must lie within the data.");
	// The file header and table alone: a whole file says nothing was found, a section has nothing to report
	const int64_t first = static_cast<int64_t>(layout.sprite_offsets[0]);
	const bytes front(layout.data.begin(), layout.data.begin() + static_cast<std::ptrdiff_t>(first));
	fixture::write("repair_table_only.pak", front);
	check_issues(pr::repair(front), {
		"[not repaired] file @ 24: Sprite table entry 0 (offset 48, length 190) matches no recoverable sprite.",
		"[not repaired] file @ 32: Sprite table entry 1 (offset 238, length 189) matches no recoverable sprite.",
		"[not repaired] file @ 40: Sprite table entry 2 (offset 427, length 178) matches no recoverable sprite.",
		"[not repaired] file @ 0: No sprites could be found.",
	});
	const PAKLib::repair_report header_section = pr::repair(layout.data, 0, first);
	check_issues(header_section, {});
	CHECK(header_section.data.sprites.empty());
	const PAKLib::repair_report empty_section = pr::repair(layout.data, 10, 10, true);
	check_issues(empty_section, {});
	CHECK(empty_section.data.sprites.empty());
	CHECK(empty_section.placeholder_sprites.empty());
}

TEST_CASE("RepairFile: path rules, then the rebuilt file is written") {
	std::vector<fixture::sprite_spec> specs = three_sprites();
	specs[1].valid_magic = false;
	const std::string input = fixture::write("repair_in.pak", fixture::build_pak(specs).data);
	const std::string output = fixture::output_dir() + "/repair_out.pak";

	CHECK_THROWS_AS(PAKLib::repair_file("", output), std::invalid_argument, "File path cannot be null or empty.");
	CHECK_THROWS_AS(PAKLib::repair_file(fixture::output_dir() + "/missing.pak", output), PAKLib::file_not_found_error, "The specified file does not exist.");
	CHECK_THROWS_AS(PAKLib::repair_file(input, " "), std::invalid_argument, "Output file path cannot be null or empty.");
	CHECK_THROWS_AS(PAKLib::repair_file(input, input), std::invalid_argument, "Output file path must differ from the input file path.");
	std::string upper = input;
	for (char& c : upper)
		if (c >= 'a' && c <= 'z')
			c = static_cast<char>(c - 'a' + 'A');
	CHECK_THROWS_AS(PAKLib::repair_file(input, upper), std::invalid_argument, "Output file path must differ from the input file path.");
	CHECK_THROWS_AS(PAKLib::repair_file(input, fixture::absolute(input)), std::invalid_argument, "Output file path must differ from the input file path.");
	CHECK_THROWS_AS(PAKLib::repair_file(input, fixture::output_dir() + "/../TestOutput/./repair_in.pak"), std::invalid_argument,
		"Output file path must differ from the input file path.");

	const PAKLib::repair_report report = PAKLib::repair_file(input, output);
	CHECK(report.success());
	CHECK(report.issues.size() == 1);
	CHECK(fixture::read(output) == fixture::build_pak(three_sprites()).data);
	CHECK(PAKLib::loadpak(output).sprites.size() == 3);

	// An absolute output path and force work too
	const std::string absolute_out = fixture::absolute(fixture::output_dir()) + "/repair_out_abs.pak";
	CHECK(PAKLib::repair_file(fixture::absolute(input), absolute_out, true).success());
	CHECK(fixture::read(absolute_out) == fixture::read(output));
}

// ---- Evidence rules (PAKRepair.FindSprites / TryFindEntryStart / CheckTable) ----

TEST_CASE("Repair: an image inside a sprite header's first 108 bytes is ignored") {
	fixture::pak_layout layout = fixture::build_pak(three_sprites());
	const bytes png = fixture::make_png_header(2, 2);                  // 45 bytes, a complete PNG to the probe
	const bytes bmp = fixture::make_bmp(1, 1);                         // 58 bytes
	std::copy(png.begin(), png.end(), layout.data.begin() + static_cast<std::ptrdiff_t>(layout.sprite_offsets[1] + 20));
	std::copy(bmp.begin(), bmp.end(), layout.data.begin() + static_cast<std::ptrdiff_t>(layout.sprite_offsets[2] + 30));
	fixture::write("repair_image_in_header.pak", layout.data);
	const PAKLib::pak original = PAKLib::pak::read(layout.data);
	for (int force = 0; force < 2; ++force) {
		const PAKLib::repair_report report = pr::repair(layout.data, force != 0);
		check_issues(report, {});
		REQUIRE(report.data.sprites.size() == 3);
		for (size_t i = 0; i < 3; ++i)
			CHECK(fixture::same_sprite(report.data.sprites[i], original.sprites[i]));
	}
}

TEST_CASE("Repair: a sprite never starts before the nearest sprite header magic") {
	// Sprite 0's image is not an image, and its 180-byte entry is a whole number of 12-byte records, so without the
	// header rule sprite 1's frames could be counted back to sprite 0's start (magic, table entry and contiguity)
	std::vector<fixture::sprite_spec> specs = three_sprites();
	specs[0].image = bytes(60, 0x33);
	fixture::pak_layout layout = fixture::build_pak(specs);
	fixture::set_u32(layout.data, layout.rect_count_offsets[1], 999);
	fixture::write("repair_header_floor.pak", layout.data);
	const PAKLib::repair_report report = pr::repair(layout.data);
	check_issues(report, {
		"[not repaired] sprite 0 @ " + n(static_cast<long long>(layout.sprite_offsets[0])) + ": Sprite header found but its image data is missing or damaged; dropped (repair with force to insert a placeholder).",
		"[repaired] sprite 1 @ " + n(static_cast<long long>(layout.rect_count_offsets[1])) + ": Rectangle count was 999, 3 frames found; rebuilt.",
	});
	REQUIRE(report.data.sprites.size() == 2);
	CHECK(report.data.sprites[0].sprite_rectangles.size() == 3);
	CHECK(report.data.sprites[0].image_data == specs[1].image);
}

TEST_CASE("Repair: a bare rectangle count of 0 needs supporting evidence") {
	// Empty table, then garbage with four zero bytes exactly where a count of 0 would sit, then a BMP
	bytes d;
	fixture::put_text(d, "<Pak file header>", 17);
	d.insert(d.end(), 3, 0);
	fixture::put_u32(d, 0);
	d.insert(d.end(), 113, 0x11);
	const size_t image = d.size();                                       // 137: a count of 0 would start the entry at 29
	std::fill(d.begin() + static_cast<std::ptrdiff_t>(image - 8), d.begin() + static_cast<std::ptrdiff_t>(image - 4), static_cast<uint8_t>(0));
	const bytes bmp = fixture::make_bmp(1, 1);
	d.insert(d.end(), bmp.begin(), bmp.end());
	fixture::write("repair_bare_zero.pak", d);
	check_issues(pr::repair(d), {
		"[repaired] file @ 20: Sprite count was 0 but 1 sprites were found; table rebuilt.",
		"[not repaired] sprite 0 @ " + n(static_cast<long long>(image)) + ": Image data found at " + n(static_cast<long long>(image)) +
			" but its sprite header and frames could not be recovered; dropped (repair with force to insert a placeholder).",
	});

	// The header magic at that start is the evidence a count of 0 needs
	const char* magic = "<Sprite File Header>";
	std::copy(magic, magic + 20, d.begin() + static_cast<std::ptrdiff_t>(image - 108));
	fixture::write("repair_bare_zero_magic.pak", d);
	const PAKLib::repair_report report = pr::repair(d);
	check_issues(report, { "[repaired] file @ 20: Sprite count was 0 but 1 sprites were found; table rebuilt." });
	REQUIRE(report.data.sprites.size() == 1);
	CHECK(report.data.sprites[0].sprite_rectangles.empty());
	CHECK(report.data.sprites[0].image_data == bmp);
}

TEST_CASE("Repair: contiguity beats a bare count match from a last frame with zero pivots") {
	// Sprite 1: magic gone, count damaged, table entry pointing into the header; its last frame's zero pivots read as
	// a count of 0 right in front of the image. The entry starting where sprite 0 ended wins and keeps all 3 frames.
	std::vector<fixture::sprite_spec> specs = three_sprites();
	specs[1].rects[2] = fixture::rect{ 1, 2, 3, 4, 0, 0 };
	specs[1].valid_magic = false;
	fixture::pak_layout layout = fixture::build_pak(specs);
	fixture::set_u32(layout.data, layout.rect_count_offsets[1], 7);
	fixture::set_u32(layout.data, layout.table_offsets[1], 0);
	fixture::write("repair_contiguity.pak", layout.data);
	const std::string s1 = n(static_cast<long long>(layout.sprite_offsets[1]));
	const PAKLib::repair_report report = pr::repair(layout.data);
	check_issues(report, {
		"[repaired] sprite 1 @ " + s1 + ": Sprite header magic was invalid; rebuilt.",
		"[repaired] sprite 1 @ " + n(static_cast<long long>(layout.rect_count_offsets[1])) + ": Rectangle count was 7, 3 frames found; rebuilt.",
		"[repaired] sprite 1 @ " + n(static_cast<long long>(layout.table_offsets[1])) + ": Sprite table offset was 0, sprite found at " + s1 + "; rebuilt.",
	});
	REQUIRE(report.data.sprites.size() == 3);
	CHECK(report.data.sprites[1].sprite_rectangles.size() == 3);
	specs[1].valid_magic = true;
	CHECK(report.data.to_bytes() == fixture::build_pak(specs).data);
}

TEST_CASE("Repair: a PNG with no IEND is incomplete and its claim stops at the next sprite header") {
	std::vector<fixture::sprite_spec> specs = three_sprites();
	const bytes full = fixture::make_png_header(3, 3);
	specs[1].image.assign(full.begin(), full.end() - 12);                // signature and IHDR, no IEND
	fixture::pak_layout layout = fixture::build_pak(specs);
	fixture::write("repair_png_no_iend.pak", layout.data);
	const std::string s1 = n(static_cast<long long>(layout.sprite_offsets[1]));
	const PAKLib::repair_report dropped = pr::repair(layout.data);
	check_issues(dropped, { "[not repaired] sprite 1 @ " + s1 + ": Image data is incomplete; dropped (repair with force to insert a placeholder)." });
	REQUIRE(dropped.data.sprites.size() == 2);
	const PAKLib::pak original = PAKLib::pak::read(layout.data);
	CHECK(fixture::same_sprite(dropped.data.sprites[1], original.sprites[2]));   // sprite 2 survives the claim

	const PAKLib::repair_report forced = pr::repair(layout.data, true);
	check_issues(forced, { "[repaired] sprite 1 @ " + s1 + ": Image data is incomplete; replaced with a 32x32 placeholder." });
	REQUIRE(forced.data.sprites.size() == 3);
	CHECK(forced.data.sprites[1].sprite_rectangles.size() == 3);

	// The last sprite: nothing follows, so the claim runs to the end of the data
	specs = three_sprites();
	specs[2].image.assign(full.begin(), full.end() - 12);
	layout = fixture::build_pak(specs);
	fixture::write("repair_png_no_iend_last.pak", layout.data);
	check_issues(pr::repair(layout.data), {
		"[not repaired] sprite 2 @ " + n(static_cast<long long>(layout.sprite_offsets[2])) + ": Image data is incomplete; dropped (repair with force to insert a placeholder).",
	});
}

TEST_CASE("Repair: duplicate table offsets, the first entry wins") {
	fixture::pak_layout layout = fixture::build_pak(three_sprites());
	fixture::set_u32(layout.data, layout.table_offsets[1], static_cast<uint32_t>(layout.sprite_offsets[0]));
	fixture::write("repair_duplicate_offset.pak", layout.data);
	const PAKLib::repair_report report = pr::repair(layout.data);
	check_issues(report, {
		"[repaired] sprite 1 @ " + n(static_cast<long long>(layout.table_offsets[1])) + ": Sprite table offset was " +
			n(static_cast<long long>(layout.sprite_offsets[0])) + ", sprite found at " + n(static_cast<long long>(layout.sprite_offsets[1])) + "; rebuilt.",
	});
	CHECK(report.data.to_bytes() == fixture::build_pak(three_sprites()).data);
}

TEST_CASE("Repair: table offsets pointing into the file header or the table are taken as the next sprite") {
	fixture::pak_layout layout = fixture::build_pak(three_sprites());
	fixture::set_u32(layout.data, layout.table_offsets[1], 10);
	fixture::set_u32(layout.data, layout.table_offsets[2], 30);
	fixture::write("repair_offset_into_header.pak", layout.data);
	const PAKLib::repair_report report = pr::repair(layout.data);
	check_issues(report, {
		"[repaired] sprite 1 @ " + n(static_cast<long long>(layout.table_offsets[1])) + ": Sprite table offset was 10, sprite found at " + n(static_cast<long long>(layout.sprite_offsets[1])) + "; rebuilt.",
		"[repaired] sprite 2 @ " + n(static_cast<long long>(layout.table_offsets[2])) + ": Sprite table offset was 30, sprite found at " + n(static_cast<long long>(layout.sprite_offsets[2])) + "; rebuilt.",
	});
	CHECK(report.data.to_bytes() == fixture::build_pak(three_sprites()).data);
}

TEST_CASE("Repair: a placeholder for a lost table entry takes its frame count from the bytes there") {
	fixture::pak_layout layout = fixture::build_pak(three_sprites());
	layout.data.insert(layout.data.end(), 200, 0x11);
	const uint32_t garbage = static_cast<uint32_t>(layout.data.size() - 200 + 8);
	add_table_entry(layout, garbage, 200);
	fixture::set_u32(layout.data, garbage + 100, 5);                     // where a rectangle count would be
	fixture::write("repair_lost_entry_count.pak", layout.data);
	const PAKLib::repair_report forced = pr::repair(layout.data, true);
	REQUIRE(forced.data.sprites.size() == 4);
	CHECK(forced.data.sprites[3].sprite_rectangles.size() == 5);
	CHECK((forced.placeholder_sprites == std::vector<int>{ 3 }));
	const std::string where = n(garbage);
	check_issues(forced, {
		"[repaired] sprite 3 @ " + where + ": Listed in the sprite table (offset " + where + ", length 200) but no sprite could be recovered there; replaced with a 32x32 placeholder.",
		"[repaired] file @ " + where + ": 200 trailing bytes after the last sprite; dropped.",
	});
}

TEST_CASE("Repair: the placeholder is PAKLib's 32x32 24-bit checkerboard, byte for byte") {
	fixture::pak_layout layout = fixture::build_pak(three_sprites());
	std::fill(layout.data.begin() + static_cast<std::ptrdiff_t>(layout.color_key_offsets[2] + 4), layout.data.end(), static_cast<uint8_t>(0));
	const PAKLib::repair_report forced = pr::repair(layout.data, true);
	REQUIRE((forced.placeholder_sprites == std::vector<int>{ 2 }));
	const PAKLib::sprite& p = forced.data.sprites[2];

	// Expected image, built independently: BITMAPFILEHEADER + BITMAPINFOHEADER, bottom-up rows of 96 bytes,
	// 8x8 cells alternating magenta (FF 00 FF as B G R) and black, magenta at (0, 0)
	bytes expected(54 + 96 * 32, 0);
	expected[0] = 'B';
	expected[1] = 'M';
	fixture::set_u32(expected, 2, static_cast<uint32_t>(expected.size()));
	fixture::set_u32(expected, 10, 54);
	fixture::set_u32(expected, 14, 40);
	fixture::set_u32(expected, 18, 32);
	fixture::set_u32(expected, 22, 32);
	expected[26] = 1;
	expected[28] = 24;
	for (int y = 0; y < 32; ++y)
		for (int x = 0; x < 32; ++x)
			if (((x / 8) + (y / 8)) % 2 == 0) {
				expected[static_cast<size_t>(54 + y * 96 + x * 3)] = 0xFF;
				expected[static_cast<size_t>(54 + y * 96 + x * 3 + 2)] = 0xFF;
			}
	CHECK(p.image_data == expected);
	CHECK(!p.has_color_key());
	REQUIRE(p.sprite_rectangles.size() == 1);   // the destroyed sprite had one frame
	const PAKLib::sprite_rect& r = p.sprite_rectangles[0];
	CHECK((r.x == 0 && r.y == 0 && r.width == 32 && r.height == 32 && r.pivotX == 0 && r.pivotY == 0));

	// Written out, the placeholder entry is the default header, one frame, no key, then that image
	const bytes out = forced.data.to_bytes();
	const PAKLib::pak back = PAKLib::pak::read(out);
	CHECK(back.sprites[2].image_data == expected);
	const size_t entry = back.sprite_entries[2].offset;
	CHECK(std::memcmp(&out[entry], "<Sprite File Header>", 20) == 0);
	CHECK(std::all_of(out.begin() + static_cast<std::ptrdiff_t>(entry + 20), out.begin() + static_cast<std::ptrdiff_t>(entry + 100), [](uint8_t b) { return b == 0; }));
	CHECK(back.sprite_entries[2].size == 100 + 4 + 12 + 4 + expected.size());
}

TEST_CASE("Repair: section mode recovers damaged sprites with the table as evidence only") {
	std::vector<fixture::sprite_spec> specs = three_sprites();
	specs[1].valid_magic = false;
	fixture::pak_layout layout = fixture::build_pak(specs);
	fixture::set_u32(layout.data, layout.rect_count_offsets[1], 999);
	fixture::write("repair_section_damaged.pak", layout.data);
	const int64_t size = static_cast<int64_t>(layout.data.size());
	const int64_t s1 = static_cast<int64_t>(layout.sprite_offsets[1]);
	const PAKLib::repair_report section = pr::repair(layout.data, s1, size);
	check_issues(section, {
		"[repaired] sprite 0 @ " + n(s1) + ": Sprite header magic was invalid; rebuilt.",
		"[repaired] sprite 0 @ " + n(s1 + 100) + ": Rectangle count was 999, 3 frames found; rebuilt.",
	});
	REQUIRE(section.data.sprites.size() == 2);
	CHECK(section.data.sprites[0].sprite_rectangles.size() == 3);

	// A section starting inside the sprite: its frames cannot reach back past the section start
	const PAKLib::repair_report inside = pr::repair(layout.data, s1 + 10, size, true);
	CHECK(inside.data.sprites.size() == 2);
	CHECK(!inside.issues.empty());

	// A section holding only the front of a sprite: the header is found, the image is not
	const PAKLib::repair_report front = pr::repair(fixture::build_pak(three_sprites()).data, s1, s1 + 50);
	check_issues(front, { "[not repaired] sprite 0 @ " + n(s1) + ": Sprite header found but its image data is missing or damaged; dropped (repair with force to insert a placeholder)." });
	CHECK(front.data.sprites.empty());
}

TEST_CASE("RepairFile: a directory is not a file") {
	const std::string dir = fixture::output_dir();
	CHECK_THROWS_AS(PAKLib::repair_file(dir, dir + "/repair_dir_out.pak"), PAKLib::file_not_found_error, "The specified file does not exist.");
	CHECK_THROWS_AS(PAKLib::loadpak(dir), PAKLib::file_not_found_error, "The specified file does not exist.");
	CHECK_THROWS_AS(PAKLib::get_sprite(dir, 0), PAKLib::file_not_found_error, "The specified file does not exist.");
}
