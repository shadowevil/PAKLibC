// Scenarios for writing (PAKData.ToBytes / Write, PAK.SaveToFile) and PAKData.ConvertImages
#include "PakFixture.h"

namespace {
	using fixture::bytes;
	using PAKLib::invalid_data_error;
	namespace si = PAKLib::sprite_image;

	uint32_t le32(const bytes& d, size_t at) {
		return d[at] | (static_cast<uint32_t>(d[at + 1]) << 8) | (static_cast<uint32_t>(d[at + 2]) << 16) | (static_cast<uint32_t>(d[at + 3]) << 24);
	}

	PAKLib::sprite make_sprite(const bytes& image, std::vector<PAKLib::sprite_rect> rects = { { 1, 2, 3, 4, -5, 6 } }) {
		PAKLib::sprite s;
		s.image_data = image;
		s.sprite_rectangles = rects;
		return s;
	}

	bytes bmp8(uint32_t w, uint32_t h, const std::vector<uint32_t>& palette) {
		fixture::bmp_spec s;
		s.width = static_cast<int32_t>(w);
		s.height = static_cast<int32_t>(h);
		s.bpp = 8;
		s.palette = palette;
		const uint32_t row = (w + 3) & ~3u;
		bytes px(row * h, 0);
		for (uint32_t y = 0; y < h; ++y)
			for (uint32_t x = 0; x < w; ++x)
				px[y * row + x] = static_cast<uint8_t>((x + y) % palette.size());
		return fixture::make_bmp_custom(s, px);
	}

	bytes rgba_png(uint32_t w, uint32_t h) {
		bytes rgba(w * h * 4);
		for (size_t i = 0; i < rgba.size(); ++i)
			rgba[i] = static_cast<uint8_t>(i * 29 + 3);
		for (size_t i = 3; i < rgba.size(); i += 8)
			rgba[i] = 0;
		return fixture::make_rgba_png(w, h, rgba);
	}
}

TEST_CASE("Write: an empty pak is the file header, a zero count and nothing else") {
	const PAKLib::pak empty{};
	const bytes out = empty.to_bytes();
	REQUIRE(out.size() == 24);
	CHECK(std::memcmp(out.data(), "<Pak file header>", 17) == 0);
	for (size_t i = 17; i < 24; ++i)
		CHECK(out[i] == 0);
	CHECK(PAKLib::pak::read(out).sprites.empty());
}

TEST_CASE("Write: sequential layout, table, zeroed padding, rects and colour key bytes") {
	PAKLib::pak p{};
	p.sprites.push_back(make_sprite(fixture::make_bmp(2, 2, 9, 8, 7)));
	p.sprites.push_back(make_sprite(fixture::make_png_header(3, 3), { { 0, 0, 0, 0, 0, 0 }, { 65535, 1, 2, 65535, -32768, 32767 } }));
	p.sprites.push_back(make_sprite(fixture::make_bmp(1, 1), {}));
	p.sprites[0].set_color_key(0x28, 0x28, 0x31);
	p.sprites[1].set_color_key(0, 0, 0);
	p.sprites[0].header.padding[5] = 0x77;   // padding is never written back
	p.header.padding[1] = 0x55;

	const bytes out = p.to_bytes();
	CHECK(std::memcmp(out.data(), "<Pak file header>", 17) == 0);
	CHECK(out[17] == 0);
	CHECK(out[18] == 0);
	CHECK(out[19] == 0);
	CHECK(le32(out, 20) == 3);
	size_t expected_offset = 24 + 3 * 8;
	for (size_t i = 0; i < 3; ++i) {
		const PAKLib::sprite& s = p.sprites[i];
		const size_t offset = le32(out, 24 + i * 8);
		const size_t size = le32(out, 24 + i * 8 + 4);
		CHECK(offset == expected_offset);
		CHECK(size == 100 + 4 + 12 * s.sprite_rectangles.size() + 4 + s.image_data.size());
		CHECK(std::memcmp(&out[offset], "<Sprite File Header>", 20) == 0);
		for (size_t k = 0; k < 80; ++k)
			CHECK(out[offset + 20 + k] == 0);
		CHECK(le32(out, offset + 100) == s.sprite_rectangles.size());
		for (size_t r = 0; r < s.sprite_rectangles.size(); ++r) {
			const uint8_t* rec = &out[offset + 104 + r * 12];
			const PAKLib::sprite_rect& rect = s.sprite_rectangles[r];
			CHECK((rec[0] | (rec[1] << 8)) == rect.x);
			CHECK((rec[2] | (rec[3] << 8)) == rect.y);
			CHECK((rec[4] | (rec[5] << 8)) == rect.width);
			CHECK((rec[6] | (rec[7] << 8)) == rect.height);
			CHECK(static_cast<int16_t>(rec[8] | (rec[9] << 8)) == rect.pivotX);
			CHECK(static_cast<int16_t>(rec[10] | (rec[11] << 8)) == rect.pivotY);
		}
		const size_t key_at = offset + 104 + 12 * s.sprite_rectangles.size();
		CHECK(bytes(out.begin() + static_cast<std::ptrdiff_t>(key_at + 4), out.begin() + static_cast<std::ptrdiff_t>(offset + size)) == s.image_data);
		expected_offset = offset + size;
	}
	CHECK(expected_offset == out.size());

	const size_t key0 = le32(out, 24) + 104 + 12;
	CHECK((bytes(out.begin() + static_cast<std::ptrdiff_t>(key0), out.begin() + static_cast<std::ptrdiff_t>(key0 + 4)) == bytes{ 0xFF, 0x28, 0x28, 0x31 }));
	const size_t key1 = le32(out, 32) + 104 + 24;
	CHECK((bytes(out.begin() + static_cast<std::ptrdiff_t>(key1), out.begin() + static_cast<std::ptrdiff_t>(key1 + 4)) == bytes{ 0xFF, 0, 0, 0 }));
	const size_t key2 = le32(out, 40) + 104;
	CHECK((bytes(out.begin() + static_cast<std::ptrdiff_t>(key2), out.begin() + static_cast<std::ptrdiff_t>(key2 + 4)) == bytes{ 0, 0, 0, 0 }));
}

TEST_CASE("Write: write then read round-trips every sprite") {
	PAKLib::pak p{};
	p.sprites.push_back(make_sprite(fixture::make_bmp(5, 3, 1, 2, 3)));
	p.sprites.push_back(make_sprite(rgba_png(4, 4), std::vector<PAKLib::sprite_rect>(7, PAKLib::sprite_rect{ 9, 8, 7, 6, -1, -2 })));
	p.sprites.push_back(make_sprite(fixture::make_png_header(9, 9), {}));
	p.sprites[1].set_color_key(0xFF, 0, 0xFF);
	const bytes out = p.to_bytes();
	const PAKLib::pak back = PAKLib::pak::read(out);
	REQUIRE(back.sprites.size() == 3);
	for (size_t i = 0; i < 3; ++i)
		CHECK(fixture::same_sprite(back.sprites[i], p.sprites[i]));
	CHECK(back.to_bytes() == out);
}

TEST_CASE("Write: a fixture PAK with zero padding and set keys is rewritten byte for byte") {
	std::vector<fixture::sprite_spec> specs(4);
	specs[1].image = fixture::make_png_header(16, 8);
	specs[1].rects.assign(7, fixture::rect{ 1, 2, 3, 4, -1, -2 });
	specs[2].color_key[0] = 0xFF;
	specs[2].color_key[1] = 0x12;
	specs[3].rects.clear();
	const fixture::pak_layout layout = fixture::build_pak(specs);
	CHECK(PAKLib::pak::read(layout.data).to_bytes() == layout.data);
}

TEST_CASE("Write: an unset key and stray alpha are written as 00 00 00 00") {
	std::vector<fixture::sprite_spec> specs(1);
	specs[0].color_key[0] = 0x00;
	specs[0].color_key[1] = 0x28;
	specs[0].color_key[2] = 0x28;
	specs[0].color_key[3] = 0x31;
	const fixture::pak_layout layout = fixture::build_pak(specs);
	const bytes out = PAKLib::pak::read(layout.data).to_bytes();
	REQUIRE(out.size() == layout.data.size());
	const size_t key = layout.color_key_offsets[0];
	CHECK((bytes(out.begin() + static_cast<std::ptrdiff_t>(key), out.begin() + static_cast<std::ptrdiff_t>(key + 4)) == bytes{ 0, 0, 0, 0 }));
}

TEST_CASE("Write: images over 4096 are refused again on write, with the sprite index") {
	PAKLib::pak p{};
	p.sprites.push_back(make_sprite(fixture::make_bmp(1, 1)));
	p.sprites.push_back(make_sprite(fixture::make_png_header(4097, 1)));
	CHECK_THROWS_AS(p.to_bytes(), invalid_data_error, "Sprite 1: Image is 4097x1; the limit is 4096x4096.");
	p.sprites[1].image_data = fixture::make_png_header(4096, 4096);
	CHECK_NOTHROW(p.to_bytes());
}

TEST_CASE("Write: BMPs that are not 24bpp are written as 24bpp; the sprite is not changed") {
	PAKLib::pak p{};
	const bytes original = bmp8(5, 3, { 0x102030, 0x405060, 0x708090 });
	p.sprites.push_back(make_sprite(original));
	p.sprites[0].set_color_key(1, 2, 3);
	const bytes out = p.to_bytes();
	CHECK(p.sprites[0].image_data == original);
	const PAKLib::pak back = PAKLib::pak::read(out);
	REQUIRE(back.sprites.size() == 1);
	CHECK(back.sprites[0].image_data == si::to_bmp24(original, 0xFF010203u));
	CHECK(si::get_bmp_bits_per_pixel(back.sprites[0].image_data) == 24);
	CHECK(PAKLib::detail::decode(back.sprites[0].image_data).rgba == PAKLib::detail::decode(original).rgba);
	CHECK(back.sprites[0].get_color_key() == 0xFF010203u);
}

TEST_CASE("Write: a BMP that cannot be converted names the sprite and the reason") {
	PAKLib::pak p{};
	bytes broken = bmp8(4, 4, { 0, 0xFFFFFF });
	broken.resize(broken.size() - 4);   // last row cut
	p.sprites.push_back(make_sprite(fixture::make_bmp(1, 1)));
	p.sprites.push_back(make_sprite(broken));
	CHECK_THROWS_AS(p.to_bytes(), invalid_data_error, "Sprite 1: cannot convert BMP to 24bpp. BMP pixel data is truncated.");
}

TEST_CASE("Write: PNG, 24bpp BMP and unknown data are written byte for byte") {
	PAKLib::pak p{};
	const bytes png = rgba_png(3, 2);
	bytes bmp24 = fixture::make_bmp(3, 3, 4, 5, 6);
	bmp24.push_back(0xEE);   // trailing byte inside the image is kept
	const bytes unknown = { 'G', 'I', 'F', '8', '9', 'a' };
	p.sprites.push_back(make_sprite(png));
	p.sprites.push_back(make_sprite(bmp24));
	p.sprites.push_back(make_sprite(unknown));
	const bytes out = p.to_bytes();
	for (size_t i = 0; i < 3; ++i) {
		const size_t offset = le32(out, 24 + i * 8);
		const size_t size = le32(out, 24 + i * 8 + 4);
		const bytes written(out.begin() + static_cast<std::ptrdiff_t>(offset + 120), out.begin() + static_cast<std::ptrdiff_t>(offset + size));
		CHECK(written == p.sprites[i].image_data);
	}
}

TEST_CASE("Write: Amp.pak is re-saved byte for byte") {
	const bytes original = fixture::read(fixture::amp_pak());
	const PAKLib::pak p = PAKLib::loadpak(fixture::amp_pak());
	const bytes out = p.to_bytes();
	CHECK(out.size() == original.size());
	size_t first_difference = out.size();
	for (size_t i = 0; i < std::min(out.size(), original.size()); ++i)
		if (out[i] != original[i]) {
			first_difference = i;
			break;
		}
	if (first_difference != out.size())
		std::printf("    note: first difference at byte %zu\n", first_difference);
	CHECK(out == original);

	const std::string saved = fixture::output_dir() + "/Amp.resaved.pak";
	PAKLib::save_to_file(p, saved);
	CHECK(fixture::read(saved) == original);
}

TEST_CASE("Write: save_to_file and write check the path and replace the file") {
	PAKLib::pak p{};
	p.sprites.push_back(make_sprite(fixture::make_bmp(1, 1)));
	CHECK_THROWS_AS(PAKLib::save_to_file(p, ""), std::invalid_argument, "File path cannot be null or empty.");
	CHECK_THROWS_AS(PAKLib::save_to_file(p, " \t"), std::invalid_argument, "File path cannot be null or empty.");

	const std::string path = fixture::write("save_target.pak", bytes(5000, 0xAA));
	PAKLib::save_to_file(p, path);
	CHECK(fixture::read(path) == p.to_bytes());
	p.sprites.push_back(make_sprite(fixture::make_png_header(2, 2)));
	p.write(path);
	CHECK(fixture::read(path) == p.to_bytes());
	CHECK(PAKLib::loadpak(path).sprites.size() == 2);

	CHECK_THROWS_WITH(p.write(fixture::output_dir() + "/no_such_dir/x.pak"), "Failed to create file");

	// A failed conversion leaves the target untouched
	PAKLib::pak bad{};
	bad.sprites.push_back(make_sprite(fixture::make_png_header(5000, 1)));
	CHECK_THROWS_AS(PAKLib::save_to_file(bad, path), invalid_data_error, "Sprite 0: Image is 5000x1; the limit is 4096x4096.");
	CHECK(fixture::read(path) == p.to_bytes());
}

TEST_CASE("ConvertImages: Unknown is refused; Bmp and Png convert every sprite with the key rules") {
	PAKLib::pak p{};
	p.sprites.push_back(make_sprite(rgba_png(4, 3)));
	p.sprites.push_back(make_sprite(bmp8(3, 2, { 0x28282F, 0x112233 })));
	p.sprites.push_back(make_sprite(fixture::make_bmp(2, 2, 7, 7, 7)));
	p.sprites[2].set_color_key(1, 1, 1);
	CHECK_THROWS_AS(p.convert_images(PAKLib::sprite_image_format::unknown), std::invalid_argument, "Target format must be Bmp or Png.");

	PAKLib::pak as_bmp = p;
	as_bmp.convert_images(PAKLib::sprite_image_format::bmp);
	for (const PAKLib::sprite& s : as_bmp.sprites)
		CHECK(si::get_bmp_bits_per_pixel(s.image_data) == 24);
	CHECK(as_bmp.sprites[0].get_color_key() == 0xFFFF00FFu);   // PNG without a key gets magenta
	CHECK(!as_bmp.sprites[1].has_color_key());                 // BMP keeps having none
	CHECK(as_bmp.sprites[2].get_color_key() == 0xFF010101u);

	PAKLib::pak as_png = p;
	as_png.convert_images(PAKLib::sprite_image_format::png);
	for (const PAKLib::sprite& s : as_png.sprites)
		CHECK(s.image_format() == PAKLib::sprite_image_format::png);
	CHECK(!as_png.sprites[0].has_color_key());                 // PNG stays without a key
	CHECK(as_png.sprites[1].get_color_key() == 0xFF112233u);   // top-left colour (BMP rows are stored bottom-up)
	CHECK(as_png.sprites[2].get_color_key() == 0xFF010101u);
	CHECK(PAKLib::detail::decode(as_png.sprites[1].image_data).rgba[3] == 0);

	// Converted paks write and read back
	CHECK(PAKLib::pak::read(as_png.to_bytes()).sprites.size() == 3);
	CHECK(PAKLib::pak::read(as_bmp.to_bytes()).sprites.size() == 3);
}

TEST_CASE("ConvertImages: Amp.pak to BMP and back to PNG keeps every pixel's colour and transparency") {
	PAKLib::pak p = PAKLib::loadpak(fixture::amp_pak());
	std::vector<PAKLib::detail::image> before;
	for (const PAKLib::sprite& s : p.sprites)
		before.push_back(PAKLib::detail::decode(s.image_data));
	p.convert_images(PAKLib::sprite_image_format::bmp);
	p.convert_images(PAKLib::sprite_image_format::png);
	REQUIRE(p.sprites.size() == before.size());
	for (size_t i = 0; i < p.sprites.size(); ++i) {
		CHECK(p.sprites[i].get_color_key() == 0xFFFF00FFu);
		const PAKLib::detail::image after = PAKLib::detail::decode(p.sprites[i].image_data);
		REQUIRE(after.rgba.size() == before[i].rgba.size());
		size_t mismatches = 0;
		for (size_t k = 0; k < after.rgba.size(); k += 4) {
			const bool was_transparent = before[i].rgba[k + 3] < 128;
			const bool is_transparent = after.rgba[k + 3] == 0;
			// Opaque magenta in the original also becomes transparent: that is PAKLib's rule
			const bool magenta = before[i].rgba[k] == 0xFF && before[i].rgba[k + 1] == 0 && before[i].rgba[k + 2] == 0xFF;
			if (was_transparent != is_transparent && !magenta)
				++mismatches;
			if (!was_transparent && !magenta && std::memcmp(&after.rgba[k], &before[i].rgba[k], 3) != 0)
				++mismatches;
		}
		CHECK(mismatches == 0);
	}
}
