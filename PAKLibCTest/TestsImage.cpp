// Scenarios for the image API (SpriteImage.cs, Sprite.ImageFormat / ConvertToBmp / ConvertToPng): format
// detection, size checks, BMP and PNG decoding, inflate / deflate, PNG encoding and the colour-key rules.
// Images are built byte by byte by the fixture; compressed vectors come from Python's zlib.
#include "PakFixture.h"
#include "ZlibVectors.h"

namespace {
	using fixture::bytes;
	using PAKLib::invalid_data_error;
	using PAKLib::sprite_image_format;
	namespace si = PAKLib::sprite_image;

	PAKLib::detail::image decode(const bytes& data) {
		return PAKLib::detail::decode(data);
	}

	bytes pattern_a() {
		bytes out;
		for (int i = 0; i < 200; ++i)
			fixture::put_text(out, "PAKLibC ", 8);
		for (int i = 0; i < 256; ++i)
			out.push_back(static_cast<uint8_t>(i));
		return out;
	}

	bytes pattern_b(size_t count) {
		bytes out(count);
		for (size_t i = 0; i < count; ++i)
			out[i] = static_cast<uint8_t>(((i % 251) ^ (i / 997)) & 0xFF);
		return out;
	}

	bytes inflate(const uint8_t* data, size_t length, size_t limit) {
		return PAKLib::detail::zlib_inflate(data, length, limit);
	}

	uint32_t adler(const bytes& data) {
		uint32_t a = 1, b = 0;
		for (uint8_t v : data) {
			a = (a + v) % 65521;
			b = (b + a) % 65521;
		}
		return (b << 16) | a;
	}

	uint32_t be32(const bytes& d, size_t at) {
		return (static_cast<uint32_t>(d[at]) << 24) | (static_cast<uint32_t>(d[at + 1]) << 16) | (static_cast<uint32_t>(d[at + 2]) << 8) | d[at + 3];
	}

	uint32_t le32(const bytes& d, size_t at) {
		return d[at] | (static_cast<uint32_t>(d[at + 1]) << 8) | (static_cast<uint32_t>(d[at + 2]) << 16) | (static_cast<uint32_t>(d[at + 3]) << 24);
	}

	// RGBA image with every pixel different, plus some fully and partly transparent pixels
	bytes test_rgba(uint32_t w, uint32_t h) {
		bytes rgba(w * h * 4);
		for (uint32_t y = 0; y < h; ++y)
			for (uint32_t x = 0; x < w; ++x) {
				const size_t o = (y * w + x) * 4;
				rgba[o] = static_cast<uint8_t>(x * 37 + y);
				rgba[o + 1] = static_cast<uint8_t>(y * 53 + x * 3);
				rgba[o + 2] = static_cast<uint8_t>(x * y + 7);
				rgba[o + 3] = static_cast<uint8_t>((x + y) % 3 == 0 ? 0 : (x + y) % 3 == 1 ? 127 : 255);
			}
		return rgba;
	}

	// Pixel rows (bottom-up) for a 24bpp BMP from top-down RGB pixels
	bytes bmp24_rows(uint32_t w, uint32_t h, const std::vector<uint32_t>& rgb) {
		const uint32_t row = (w * 3 + 3) & ~3u;
		bytes out(row * h, 0);
		for (uint32_t y = 0; y < h; ++y)
			for (uint32_t x = 0; x < w; ++x) {
				const size_t o = (h - 1 - y) * row + x * 3;
				const uint32_t c = rgb[y * w + x];
				out[o] = static_cast<uint8_t>(c);
				out[o + 1] = static_cast<uint8_t>(c >> 8);
				out[o + 2] = static_cast<uint8_t>(c >> 16);
			}
		return out;
	}

	// 0xRRGGBBFF
	uint32_t opaque(uint32_t rgb) { return (rgb << 8) | 0xFF; }

	bytes simple_png(uint8_t color_type, uint8_t bit_depth, uint32_t w, uint32_t h, uint8_t interlace = 0, int filter = 0,
		const bytes& plte = bytes(), const bytes& trns = bytes()) {
		fixture::png_spec s;
		s.width = w;
		s.height = h;
		s.color_type = color_type;
		s.bit_depth = bit_depth;
		s.interlace = interlace;
		s.plte = plte;
		s.trns = trns;
		const uint32_t max = (1u << bit_depth) - 1;
		const bytes lines = fixture::png_scanlines(s, [&](uint32_t x, uint32_t y, int c) {
			return (x * 7 + y * 13 + static_cast<uint32_t>(c) * 5 + x * y) % (max + 1);
		}, filter);
		return fixture::make_png(s, fixture::zlib_stored(lines));
	}

	uint32_t sample_value(uint32_t x, uint32_t y, int c, uint8_t bit_depth) {
		const uint32_t max = (1u << bit_depth) - 1;
		return (x * 7 + y * 13 + static_cast<uint32_t>(c) * 5 + x * y) % (max + 1);
	}

	uint8_t scale(uint32_t v, uint8_t bit_depth) {
		return static_cast<uint8_t>(bit_depth == 16 ? v >> 8 : v * 255 / ((1u << bit_depth) - 1));
	}
}

// ---- Format, size and bit depth ----

TEST_CASE("Image: constants match PAKLib") {
	CHECK(si::default_color_key == 0xFFFF00FFu);
	CHECK(si::max_dimension == 4096);
}

TEST_CASE("Image: get_format detects PNG, BMP and unknown data") {
	CHECK(si::get_format(fixture::make_png_header(1, 1)) == sprite_image_format::png);
	CHECK(si::get_format(fixture::make_bmp(1, 1)) == sprite_image_format::bmp);
	CHECK(si::get_format(bytes{ 'B', 'M' }) == sprite_image_format::bmp);
	CHECK(si::get_format(bytes{ 'B' }) == sprite_image_format::unknown);
	CHECK(si::get_format(bytes{}) == sprite_image_format::unknown);
	CHECK(si::get_format(bytes{ 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A }) == sprite_image_format::unknown);
	CHECK(si::get_format(bytes{ 'G', 'I', 'F', '8', '9', 'a', 0, 0 }) == sprite_image_format::unknown);
	PAKLib::sprite s;
	s.image_data = fixture::make_png_header(2, 2);
	CHECK(s.image_format() == sprite_image_format::png);
	s.image_data = fixture::make_bmp(2, 2);
	CHECK(s.image_format() == sprite_image_format::bmp);
	s.image_data.clear();
	CHECK(s.image_format() == sprite_image_format::unknown);
}

TEST_CASE("Image: try_get_size reads PNG, BMP and OS/2 BMP headers") {
	int w = -1, h = -1;
	CHECK(si::try_get_size(fixture::make_png_header(640, 480), w, h));
	CHECK(w == 640);
	CHECK(h == 480);
	const bytes small_png = fixture::make_png_header(1, 1);
	CHECK(!si::try_get_size(bytes(small_png.begin(), small_png.begin() + 23), w, h));
	CHECK(w == 0);
	CHECK(h == 0);

	bytes bmp = fixture::make_bmp(7, 3);
	CHECK(si::try_get_size(bmp, w, h));
	CHECK(w == 7);
	CHECK(h == 3);
	fixture::set_u32(bmp, 22, static_cast<uint32_t>(-3));     // top-down
	CHECK(si::try_get_size(bmp, w, h));
	CHECK(h == 3);
	fixture::set_u32(bmp, 22, 0x80000000u);                   // abs(int.MinValue) is clamped
	CHECK(si::try_get_size(bmp, w, h));
	CHECK(h == 2147483647);

	bytes core(26, 0);                                         // BITMAPCOREHEADER: 16-bit fields
	core[0] = 'B';
	core[1] = 'M';
	fixture::set_u32(core, 14, 12);
	core[18] = 0x34;
	core[19] = 0x12;
	core[20] = 0xFE;                                            // height -2 as int16
	core[21] = 0xFF;
	CHECK(si::try_get_size(core, w, h));
	CHECK(w == 0x1234);
	CHECK(h == 2);
	CHECK(!si::try_get_size(bytes(core.begin(), core.begin() + 25), w, h));
	CHECK(!si::try_get_size(bytes(30, 0), w, h));
}

TEST_CASE("Image: check_size throws past 4096 on either side, with PAKLib's message") {
	CHECK_NOTHROW(si::check_size(fixture::make_png_header(4096, 4096)));
	CHECK_THROWS_AS(si::check_size(fixture::make_png_header(4097, 4096)), invalid_data_error, "Image is 4097x4096; the limit is 4096x4096.");
	CHECK_THROWS_AS(si::check_size(fixture::make_png_header(1, 70000)), invalid_data_error, "Image is 1x70000; the limit is 4096x4096.");
	bytes bmp = fixture::make_bmp(1, 1);
	fixture::set_u32(bmp, 22, static_cast<uint32_t>(-5000));
	CHECK_THROWS_AS(si::check_size(bmp), invalid_data_error, "Image is 1x5000; the limit is 4096x4096.");
	CHECK_NOTHROW(si::check_size(bytes{ 1, 2, 3 }));
	CHECK_NOTHROW(si::check_size(fixture::make_png_header(0x80000000u, 1)));
}

TEST_CASE("Image: get_bmp_bits_per_pixel reads both header kinds and needs 30 bytes") {
	CHECK(si::get_bmp_bits_per_pixel(fixture::make_bmp(1, 1)) == 24);
	fixture::bmp_spec s;
	s.bpp = 8;
	s.palette = { 0 };
	CHECK(si::get_bmp_bits_per_pixel(fixture::make_bmp_custom(s, bytes(4, 0))) == 8);
	bytes core(30, 0);
	core[0] = 'B';
	core[1] = 'M';
	fixture::set_u32(core, 14, 12);
	core[24] = 4;
	core[28] = 32;
	CHECK(si::get_bmp_bits_per_pixel(core) == 4);
	CHECK(si::get_bmp_bits_per_pixel(bytes(core.begin(), core.begin() + 29)) == 0);
	CHECK(si::get_bmp_bits_per_pixel(fixture::make_png_header(1, 1)) == 0);
}

// ---- BMP decoding ----

TEST_CASE("BMP: 24bpp bottom-up and top-down with row padding") {
	const std::vector<uint32_t> rgb = { 0xFF0000, 0x00FF00, 0x0000FF, 0x123456, 0xABCDEF, 0x010203 };
	fixture::bmp_spec s;
	s.width = 3;
	s.height = 2;
	const bytes bmp = fixture::make_bmp_custom(s, bmp24_rows(3, 2, rgb));
	const PAKLib::detail::image img = decode(bmp);
	REQUIRE(img.width == 3);
	REQUIRE(img.height == 2);
	for (int i = 0; i < 6; ++i)
		CHECK(fixture::rgba_at(img, i % 3, i / 3) == opaque(rgb[static_cast<size_t>(i)]));

	// Top-down: same rows stored in the other order, negative height
	bytes rows = bmp24_rows(3, 2, rgb);
	bytes flipped(rows.begin() + 12, rows.end());
	flipped.insert(flipped.end(), rows.begin(), rows.begin() + 12);
	s.height = -2;
	const PAKLib::detail::image top = decode(fixture::make_bmp_custom(s, flipped));
	REQUIRE(top.height == 2);
	CHECK(top.rgba == img.rgba);
}

TEST_CASE("BMP: 1, 4 and 8 bpp palettes, including indices past the palette") {
	// 1 bpp, width 10 (2 bytes + 2 padding per row)
	fixture::bmp_spec s1;
	s1.width = 10;
	s1.height = 1;
	s1.bpp = 1;
	s1.palette = { 0x000000, 0xFFFFFF };
	const PAKLib::detail::image one = decode(fixture::make_bmp_custom(s1, bytes{ 0xA5, 0x80, 0, 0 }));
	const int bits[10] = { 1, 0, 1, 0, 0, 1, 0, 1, 1, 0 };
	for (int x = 0; x < 10; ++x)
		CHECK(fixture::rgba_at(one, x, 0) == (bits[x] ? 0xFFFFFFFFu : 0x000000FFu));

	// 4 bpp, width 3
	fixture::bmp_spec s4;
	s4.width = 3;
	s4.height = 1;
	s4.bpp = 4;
	for (uint32_t i = 0; i < 16; ++i)
		s4.palette.push_back(i * 0x111111);
	const PAKLib::detail::image four = decode(fixture::make_bmp_custom(s4, bytes{ 0x2F, 0x70, 0, 0 }));
	CHECK(fixture::rgba_at(four, 0, 0) == opaque(0x222222));
	CHECK(fixture::rgba_at(four, 1, 0) == opaque(0xFFFFFF));
	CHECK(fixture::rgba_at(four, 2, 0) == opaque(0x777777));

	// 8 bpp with colors_used = 3: index 5 is past the palette and reads as black
	fixture::bmp_spec s8;
	s8.width = 4;
	s8.height = 1;
	s8.bpp = 8;
	s8.colors_used = 3;
	s8.palette = { 0x102030, 0x405060, 0x708090 };
	const PAKLib::detail::image eight = decode(fixture::make_bmp_custom(s8, bytes{ 2, 1, 0, 5 }));
	CHECK(fixture::rgba_at(eight, 0, 0) == opaque(0x708090));
	CHECK(fixture::rgba_at(eight, 1, 0) == opaque(0x405060));
	CHECK(fixture::rgba_at(eight, 2, 0) == opaque(0x102030));
	CHECK(fixture::rgba_at(eight, 3, 0) == opaque(0x000000));
}

TEST_CASE("BMP: a palette cut short by the end of the file reads as black") {
	fixture::bmp_spec s;
	s.width = 1;
	s.height = 1;
	s.bpp = 8;
	s.palette = { 0x112233 };
	bytes bmp = fixture::make_bmp_custom(s, bytes{ 255, 0, 0, 0 });   // 256 entries assumed; entry 255 lies past the data
	const PAKLib::detail::image img = decode(bmp);
	CHECK(fixture::rgba_at(img, 0, 0) == opaque(0));
}

TEST_CASE("BMP: 16 bpp 5-5-5 default and 5-6-5 bitfields") {
	fixture::bmp_spec s;
	s.width = 2;
	s.height = 1;
	s.bpp = 16;
	bytes px;
	fixture::put_u16(px, 0x7C00);                  // red 31
	fixture::put_u16(px, (16 << 5) | 1);           // green 16, blue 1
	const PAKLib::detail::image img = decode(fixture::make_bmp_custom(s, px));
	CHECK(fixture::rgba_at(img, 0, 0) == 0xFF0000FFu);
	CHECK(fixture::rgba_at(img, 1, 0) == ((0u << 24) | (static_cast<uint32_t>(16 * 255 / 31) << 16) | (static_cast<uint32_t>(1 * 255 / 31) << 8) | 0xFF));

	s.compression = 3;
	s.masks = true;
	s.mask_r = 0xF800;
	s.mask_g = 0x07E0;
	s.mask_b = 0x001F;
	bytes px565;
	fixture::put_u16(px565, (31u << 11) | (63u << 5) | 0);
	fixture::put_u16(px565, (0u << 11) | (32u << 5) | 31);
	const PAKLib::detail::image bf = decode(fixture::make_bmp_custom(s, px565));
	CHECK(fixture::rgba_at(bf, 0, 0) == 0xFFFF00FFu);
	CHECK(fixture::rgba_at(bf, 1, 0) == ((static_cast<uint32_t>(32 * 255 / 63) << 16) | (255u << 8) | 0xFF));
}

TEST_CASE("BMP: 32 bpp default, bitfields and alpha bitfields, alpha always dropped") {
	fixture::bmp_spec s;
	s.width = 1;
	s.height = 1;
	s.bpp = 32;
	bytes px;
	fixture::put_u32(px, 0x80112233u);   // A=80 R=11 G=22 B=33
	CHECK(fixture::rgba_at(decode(fixture::make_bmp_custom(s, px)), 0, 0) == 0x112233FFu);

	s.compression = 3;
	s.masks = true;
	s.mask_r = 0x000000FF;               // swapped channels
	s.mask_g = 0x0000FF00;
	s.mask_b = 0x00FF0000;
	CHECK(fixture::rgba_at(decode(fixture::make_bmp_custom(s, px)), 0, 0) == 0x332211FFu);

	s.compression = 6;
	s.mask_r = 0;                        // a zero mask gives 0
	CHECK(fixture::rgba_at(decode(fixture::make_bmp_custom(s, px)), 0, 0) == 0x002211FFu);
}

TEST_CASE("BMP: RLE8 runs, absolute runs, delta, end of line and end of bitmap; skipped pixels are index 0") {
	fixture::bmp_spec s;
	s.width = 4;
	s.height = 3;
	s.bpp = 8;
	s.compression = 1;
	s.palette = { 0x101010, 0xFF0000, 0x00FF00, 0x0000FF };
	const bytes rle = {
		0x03, 0x01, 0x00, 0x00,                          // bottom row: 1 1 1, end of line
		0x00, 0x03, 0x02, 0x03, 0x01, 0x00,              // absolute 2 3 1 (padded)
		0x01, 0x02, 0x00, 0x00,                          // 2, end of line
		0x00, 0x02, 0x01, 0x00,                          // delta right 1
		0x02, 0x03, 0x00, 0x01,                          // 3 3, end of bitmap
	};
	const PAKLib::detail::image img = decode(fixture::make_bmp_custom(s, rle));
	const uint32_t p0 = opaque(0x101010), p1 = opaque(0xFF0000), p2 = opaque(0x00FF00), p3 = opaque(0x0000FF);
	const uint32_t expected[3][4] = { { p0, p3, p3, p0 }, { p2, p3, p1, p2 }, { p1, p1, p1, p0 } };
	for (int y = 0; y < 3; ++y)
		for (int x = 0; x < 4; ++x)
			CHECK(fixture::rgba_at(img, x, y) == expected[y][x]);
}

TEST_CASE("BMP: RLE8 runs past the row edge and deltas past the top are clipped") {
	fixture::bmp_spec s;
	s.width = 2;
	s.height = 2;
	s.bpp = 8;
	s.compression = 1;
	s.palette = { 0x000000, 0xFFFFFF };
	const bytes rle = { 0x05, 0x01, 0x00, 0x02, 0x00, 0x09, 0x04, 0x01 };
	const PAKLib::detail::image img = decode(fixture::make_bmp_custom(s, rle));
	CHECK(fixture::rgba_at(img, 0, 1) == 0xFFFFFFFFu);
	CHECK(fixture::rgba_at(img, 1, 1) == 0xFFFFFFFFu);
	CHECK(fixture::rgba_at(img, 0, 0) == 0x000000FFu);
	CHECK(fixture::rgba_at(img, 1, 0) == 0x000000FFu);
}

TEST_CASE("BMP: RLE4 encoded and absolute runs") {
	fixture::bmp_spec s;
	s.width = 5;
	s.height = 2;
	s.bpp = 4;
	s.compression = 2;
	for (uint32_t i = 0; i < 16; ++i)
		s.palette.push_back(i * 0x101010);
	const bytes rle = {
		0x05, 0x12, 0x00, 0x00,                          // bottom: 1 2 1 2 1
		0x00, 0x03, 0xAB, 0xC0,                          // top: absolute A B C
		0x00, 0x01,
	};
	const PAKLib::detail::image img = decode(fixture::make_bmp_custom(s, rle));
	const uint32_t bottom[5] = { 1, 2, 1, 2, 1 };
	for (int x = 0; x < 5; ++x)
		CHECK(fixture::rgba_at(img, x, 1) == opaque(bottom[x] * 0x101010));
	CHECK(fixture::rgba_at(img, 0, 0) == opaque(0xA * 0x101010));
	CHECK(fixture::rgba_at(img, 1, 0) == opaque(0xB * 0x101010));
	CHECK(fixture::rgba_at(img, 2, 0) == opaque(0xC * 0x101010));
	CHECK(fixture::rgba_at(img, 3, 0) == opaque(0));
	CHECK(fixture::rgba_at(img, 4, 0) == opaque(0));
}

TEST_CASE("BMP: OS/2 core header with a 3-byte palette and a top-down 16-bit height") {
	bytes bmp;
	fixture::put_text(bmp, "BM", 2);
	fixture::put_u32(bmp, 0);
	fixture::put_u32(bmp, 0);
	fixture::put_u32(bmp, 14 + 12 + 6);   // pixel offset after a 2-entry palette
	fixture::put_u32(bmp, 12);
	fixture::put_u16(bmp, 2);             // width
	fixture::put_u16(bmp, static_cast<uint16_t>(-2));   // height -2: top-down
	fixture::put_u16(bmp, 1);
	fixture::put_u16(bmp, 8);
	const uint8_t palette[6] = { 0x33, 0x22, 0x11, 0x66, 0x55, 0x44 };
	bmp.insert(bmp.end(), palette, palette + 6);
	const uint8_t pixels[8] = { 0, 1, 0, 0, 1, 1, 0, 0 };
	bmp.insert(bmp.end(), pixels, pixels + 8);
	const PAKLib::detail::image img = decode(bmp);
	REQUIRE(img.height == 2);
	CHECK(fixture::rgba_at(img, 0, 0) == opaque(0x112233));
	CHECK(fixture::rgba_at(img, 1, 0) == opaque(0x445566));
	CHECK(fixture::rgba_at(img, 0, 1) == opaque(0x445566));
	CHECK(fixture::rgba_at(img, 1, 1) == opaque(0x445566));
}

TEST_CASE("BMP: every decode error carries PAKLib's message") {
	CHECK_THROWS_AS(decode(bytes{ 'B', 'M', 0, 0 }), invalid_data_error, "BMP header is truncated.");

	bytes dib20 = fixture::make_bmp(1, 1);
	fixture::set_u32(dib20, 14, 20);
	CHECK_THROWS_AS(decode(dib20), invalid_data_error, "Unsupported BMP header size 20.");
	bytes short40 = fixture::make_bmp(1, 1);
	short40.resize(40);
	CHECK_THROWS_AS(decode(short40), invalid_data_error, "Unsupported BMP header size 40.");

	fixture::bmp_spec s;
	s.bpp = 2;
	CHECK_THROWS_AS(decode(fixture::make_bmp_custom(s, bytes(4, 0))), invalid_data_error, "Unsupported BMP bit depth 2.");
	s.bpp = 24;
	s.compression = 4;
	CHECK_THROWS_AS(decode(fixture::make_bmp_custom(s, bytes(4, 0))), invalid_data_error, "Unsupported BMP compression 4.");
	s.compression = 0;
	CHECK_THROWS_AS(decode(fixture::make_bmp_custom(s, bytes(3, 0))), invalid_data_error, "BMP pixel data is truncated.");
	s.width = 0;
	CHECK_THROWS_AS(decode(fixture::make_bmp_custom(s, bytes())), invalid_data_error, "Unsupported image size 0x1.");
	s.width = 5000;
	CHECK_THROWS_AS(decode(fixture::make_bmp_custom(s, bytes())), invalid_data_error, "BMP pixel data is truncated.");
	CHECK_THROWS_AS(decode(fixture::make_bmp_custom(s, bytes(15000, 0))), invalid_data_error, "Image is 5000x1; the limit is 4096x4096.");

	// Bitfield masks are needed even when the header is complete
	bytes masked = fixture::make_bmp(1, 1);
	fixture::set_u32(masked, 30, 3);
	masked[28] = 16;
	CHECK_THROWS_AS(decode(masked), invalid_data_error, "BMP bitfield masks are truncated.");
	CHECK_THROWS_AS(decode(bytes{ 1, 2, 3 }), invalid_data_error, "Image data is neither BMP nor PNG.");
}

// ---- PNG decoding ----

TEST_CASE("PNG: grayscale at 1, 2, 4, 8 and 16 bits, scaled to 8 bits") {
	const uint8_t depths[5] = { 1, 2, 4, 8, 16 };
	for (uint8_t depth : depths) {
		const PAKLib::detail::image img = decode(simple_png(0, depth, 9, 3));
		REQUIRE(img.width == 9);
		for (uint32_t y = 0; y < 3; ++y)
			for (uint32_t x = 0; x < 9; ++x) {
				const uint8_t g = scale(sample_value(x, y, 0, depth), depth);
				CHECK(fixture::rgba_at(img, static_cast<int>(x), static_cast<int>(y)) == ((static_cast<uint32_t>(g) << 24) | (g << 16) | (g << 8) | 0xFF));
			}
	}
}

TEST_CASE("PNG: RGB, gray+alpha and RGBA at 8 and 16 bits") {
	const uint8_t types[3] = { 2, 4, 6 };
	const uint8_t depths[2] = { 8, 16 };
	for (uint8_t type : types)
		for (uint8_t depth : depths) {
			const PAKLib::detail::image img = decode(simple_png(type, depth, 5, 4));
			for (uint32_t y = 0; y < 4; ++y)
				for (uint32_t x = 0; x < 5; ++x) {
					uint32_t expected;
					if (type == 4) {
						const uint32_t g = scale(sample_value(x, y, 0, depth), depth);
						expected = (g << 24) | (g << 16) | (g << 8) | scale(sample_value(x, y, 1, depth), depth);
					}
					else {
						expected = (static_cast<uint32_t>(scale(sample_value(x, y, 0, depth), depth)) << 24) |
							(static_cast<uint32_t>(scale(sample_value(x, y, 1, depth), depth)) << 16) |
							(static_cast<uint32_t>(scale(sample_value(x, y, 2, depth), depth)) << 8) |
							(type == 6 ? scale(sample_value(x, y, 3, depth), depth) : 0xFFu);
					}
					CHECK(fixture::rgba_at(img, static_cast<int>(x), static_cast<int>(y)) == expected);
				}
		}
}

TEST_CASE("PNG: palette at 1, 2, 4 and 8 bits with partial tRNS and indices past PLTE") {
	const uint8_t depths[4] = { 1, 2, 4, 8 };
	for (uint8_t depth : depths) {
		const uint32_t entries = std::min<uint32_t>(1u << depth, 3);   // fewer entries than indices for depth >= 2
		bytes plte;
		for (uint32_t i = 0; i < entries; ++i) {
			plte.push_back(static_cast<uint8_t>(10 + i));
			plte.push_back(static_cast<uint8_t>(20 + i));
			plte.push_back(static_cast<uint8_t>(30 + i));
		}
		const bytes trns = { 0x00, 0x80 };   // index 0 transparent, 1 half, the rest opaque
		const PAKLib::detail::image img = decode(simple_png(3, depth, 7, 2, 0, 0, plte, trns));
		for (uint32_t y = 0; y < 2; ++y)
			for (uint32_t x = 0; x < 7; ++x) {
				const uint32_t index = sample_value(x, y, 0, depth);
				const uint32_t rgb = index < entries ? ((10 + index) << 16) | ((20 + index) << 8) | (30 + index) : 0;
				const uint32_t alpha = index == 0 ? 0 : index == 1 ? 0x80 : 0xFF;
				CHECK(fixture::rgba_at(img, static_cast<int>(x), static_cast<int>(y)) == ((rgb << 8) | alpha));
			}
	}
}

TEST_CASE("PNG: tRNS on grayscale and RGB matches the raw sample value") {
	fixture::png_spec g;
	g.width = 3;
	g.height = 1;
	g.color_type = 0;
	g.bit_depth = 16;
	g.trns = { 0x12, 0x34 };
	const uint32_t gray[3] = { 0x1234, 0x1235, 0xFFFF };
	const bytes gray_png = fixture::make_png(g, fixture::zlib_stored(fixture::png_scanlines(g, [&](uint32_t x, uint32_t, int) { return gray[x]; })));
	const PAKLib::detail::image gi = decode(gray_png);
	CHECK(fixture::rgba_at(gi, 0, 0) == 0x12121200u);
	CHECK(fixture::rgba_at(gi, 1, 0) == 0x121212FFu);
	CHECK(fixture::rgba_at(gi, 2, 0) == 0xFFFFFFFFu);

	fixture::png_spec c;
	c.width = 2;
	c.height = 1;
	c.color_type = 2;
	c.trns = { 0, 1, 0, 2, 0, 3 };
	const uint32_t rgb[2][3] = { { 1, 2, 3 }, { 1, 2, 4 } };
	const bytes rgb_png = fixture::make_png(c, fixture::zlib_stored(fixture::png_scanlines(c, [&](uint32_t x, uint32_t, int ch) { return rgb[x][ch]; })));
	const PAKLib::detail::image ci = decode(rgb_png);
	CHECK(fixture::rgba_at(ci, 0, 0) == 0x01020300u);
	CHECK(fixture::rgba_at(ci, 1, 0) == 0x010204FFu);

	// A tRNS too short for its colour type does not apply
	c.trns = { 0, 1, 0, 2 };
	const bytes short_trns = fixture::make_png(c, fixture::zlib_stored(fixture::png_scanlines(c, [&](uint32_t x, uint32_t, int ch) { return rgb[x][ch]; })));
	CHECK(fixture::rgba_at(decode(short_trns), 0, 0) == 0x010203FFu);
}

TEST_CASE("PNG: Adam7 interlacing decodes to the same pixels as non-interlaced") {
	struct size_case { uint32_t w, h; };
	const size_case sizes[] = { { 1, 1 }, { 2, 3 }, { 3, 5 }, { 8, 8 }, { 9, 9 }, { 17, 6 } };
	const uint8_t formats[][2] = { { 6, 8 }, { 0, 1 }, { 2, 16 }, { 3, 4 }, { 4, 8 } };
	bytes plte;
	for (int i = 0; i < 16; ++i) {
		plte.push_back(static_cast<uint8_t>(i * 16));
		plte.push_back(static_cast<uint8_t>(255 - i));
		plte.push_back(static_cast<uint8_t>(i));
	}
	for (const size_case& sz : sizes)
		for (const auto& f : formats) {
			const bytes p = f[0] == 3 ? plte : bytes();
			const PAKLib::detail::image flat = decode(simple_png(f[0], f[1], sz.w, sz.h, 0, 0, p));
			const PAKLib::detail::image laced = decode(simple_png(f[0], f[1], sz.w, sz.h, 1, 5, p));
			CHECK(flat.width == laced.width);
			CHECK(flat.rgba == laced.rgba);
		}
}

TEST_CASE("PNG: filters None, Sub, Up, Average and Paeth at 1-, 1-, 4- and 8-byte steps") {
	const uint8_t formats[][2] = { { 0, 1 }, { 0, 8 }, { 6, 8 }, { 6, 16 }, { 2, 8 } };
	for (const auto& f : formats) {
		const PAKLib::detail::image plain = decode(simple_png(f[0], f[1], 11, 10, 0, 0));
		for (int filter = 1; filter <= 5; ++filter)
			CHECK(decode(simple_png(f[0], f[1], 11, 10, 0, filter)).rgba == plain.rgba);
	}
}

TEST_CASE("PNG: IDAT split over several chunks, unknown chunks skipped") {
	const bytes rgba = test_rgba(13, 7);
	fixture::png_spec s;
	s.width = 13;
	s.height = 7;
	const bytes z = fixture::zlib_stored(fixture::png_scanlines(s, [&](uint32_t x, uint32_t y, int c) { return static_cast<uint32_t>(rgba[(y * 13 + x) * 4 + static_cast<uint32_t>(c)]); }), 50);
	bytes png = fixture::make_png(s, z, 5);
	// Insert an unknown ancillary chunk after IHDR
	bytes extra;
	fixture::put_png_chunk(extra, "tEXt", bytes{ 'a', 0, 'b' });
	png.insert(png.begin() + 33, extra.begin(), extra.end());
	CHECK(decode(png).rgba == rgba);
}

TEST_CASE("PNG: every decode error carries PAKLib's message") {
	bytes bad_type = simple_png(6, 8, 2, 2);
	bad_type[25] = 5;
	CHECK_THROWS_AS(decode(bad_type), invalid_data_error, "Unsupported PNG color type 5.");
	bytes bad_depth = simple_png(6, 8, 2, 2);
	bad_depth[24] = 3;
	CHECK_THROWS_AS(decode(bad_depth), invalid_data_error, "Unsupported PNG bit depth 3.");
	bytes no_ihdr = simple_png(6, 8, 2, 2);
	no_ihdr[12] = 'i';                               // chunk renamed: no header is read
	CHECK_THROWS_AS(decode(no_ihdr), invalid_data_error, "Unsupported PNG bit depth 0.");

	bytes cut = simple_png(6, 8, 2, 2);
	cut.resize(cut.size() - 20);                    // IDAT runs past the end
	CHECK_THROWS_AS(decode(cut), invalid_data_error, "PNG chunk IDAT is truncated.");
	bytes high_type = simple_png(6, 8, 2, 2);
	high_type[37] = 0xC9;                            // non-ASCII chunk type byte is shown as '?'
	high_type.resize(high_type.size() - 20);
	CHECK_THROWS_WITH(decode(high_type), "PNG chunk ?DAT is truncated.");

	CHECK_THROWS_AS(decode(fixture::make_png_header(2, 2)), invalid_data_error, "PNG image data is truncated.");
	fixture::png_spec s;
	s.width = 2;
	s.height = 2;
	bytes lines = fixture::png_scanlines(s, [](uint32_t, uint32_t, int) { return 1u; });
	lines[9] = 7;                                    // second row's filter byte
	CHECK_THROWS_AS(decode(fixture::make_png(s, fixture::zlib_stored(lines))), invalid_data_error, "Unsupported PNG filter 7.");
	lines.resize(12);
	lines[9] = 0;
	CHECK_THROWS_AS(decode(fixture::make_png(s, fixture::zlib_stored(lines))), invalid_data_error, "PNG image data is truncated.");

	bytes zlib = fixture::zlib_stored(bytes(18, 0));
	zlib[0] = 0x79;
	// .NET reports every zlib data error as an unwrapped InvalidDataException with this text
	CHECK_THROWS_AS(decode(fixture::make_png(s, zlib)), invalid_data_error, "The archive entry was compressed using an unsupported compression method.");

	s.width = 0;
	CHECK_THROWS_AS(decode(fixture::make_png(s, fixture::zlib_stored(bytes(1, 0)))), invalid_data_error, "Unsupported image size 0x2.");
	s.width = 5000;
	CHECK_THROWS_AS(decode(fixture::make_png(s, fixture::zlib_stored(bytes(1, 0)))), invalid_data_error, "Image is 5000x2; the limit is 4096x4096.");
}

TEST_CASE("PNG: inflate stops at the image size, so trailing or extra data is ignored") {
	fixture::png_spec s;
	s.width = 2;
	s.height = 2;
	bytes lines = fixture::png_scanlines(s, [](uint32_t x, uint32_t y, int c) { return x + y * 2 + static_cast<uint32_t>(c); });
	const PAKLib::detail::image plain = decode(fixture::make_png(s, fixture::zlib_stored(lines)));
	lines.insert(lines.end(), 100000, 0xAB);        // far more data than the image needs
	CHECK(decode(fixture::make_png(s, fixture::zlib_stored(lines))).rgba == plain.rgba);
}

// ---- Inflate / deflate ----

TEST_CASE("Inflate: Python zlib streams (dynamic, fixed, stored, Huffman-only) decode exactly") {
	const bytes a = pattern_a();
	const bytes b = pattern_b(40000);
	CHECK(inflate(zlib_vectors::zlib_a_dynamic, sizeof(zlib_vectors::zlib_a_dynamic), a.size()) == a);
	CHECK(inflate(zlib_vectors::zlib_a_fixed, sizeof(zlib_vectors::zlib_a_fixed), a.size()) == a);
	CHECK(inflate(zlib_vectors::zlib_a_stored, sizeof(zlib_vectors::zlib_a_stored), 300) == bytes(a.begin(), a.begin() + 300));
	CHECK(inflate(zlib_vectors::zlib_b_dynamic, sizeof(zlib_vectors::zlib_b_dynamic), b.size()) == b);
	CHECK(inflate(zlib_vectors::zlib_b_huffman_only, sizeof(zlib_vectors::zlib_b_huffman_only), 1500) == bytes(b.begin(), b.begin() + 1500));
	// Bounded: asking for less returns exactly that much; asking for more returns what there is
	CHECK(inflate(zlib_vectors::zlib_b_dynamic, sizeof(zlib_vectors::zlib_b_dynamic), 1234) == bytes(b.begin(), b.begin() + 1234));
	CHECK(inflate(zlib_vectors::zlib_a_dynamic, sizeof(zlib_vectors::zlib_a_dynamic), a.size() + 100) == a);
	// Cut input gives a prefix, not an error
	const bytes prefix = inflate(zlib_vectors::zlib_b_dynamic, sizeof(zlib_vectors::zlib_b_dynamic) / 2, b.size());
	CHECK(!prefix.empty());
	CHECK(prefix.size() < b.size());
	CHECK(prefix == bytes(b.begin(), b.begin() + static_cast<std::ptrdiff_t>(prefix.size())));
	CHECK(inflate(zlib_vectors::zlib_a_dynamic, 1, 10).empty());
}

TEST_CASE("Inflate: malformed streams throw invalid_data_error with .NET's messages") {
	const std::string data_error = "The archive entry was compressed using an unsupported compression method.";
	const uint8_t bad_block[] = { 0x78, 0x01, 0x07 };                      // final, block type 3
	CHECK_THROWS_AS(inflate(bad_block, 3, 10), invalid_data_error, data_error);
	const uint8_t bad_stored[] = { 0x78, 0x01, 0x01, 0x05, 0x00, 0x00, 0x00 };
	CHECK_THROWS_AS(inflate(bad_stored, sizeof(bad_stored), 10), invalid_data_error, data_error);
	const uint8_t bad_check[] = { 0x78, 0x02, 0x01, 0x00, 0x00 };          // header check bits wrong
	CHECK_THROWS_AS(inflate(bad_check, sizeof(bad_check), 10), invalid_data_error, data_error);
	const uint8_t bad_window[] = { 0x88, 0x1C, 0x01, 0x00, 0x00 };         // CINFO 8: window larger than 32 KiB
	CHECK_THROWS_AS(inflate(bad_window, sizeof(bad_window), 10), invalid_data_error, data_error);
	// FDICT: zlib reads the 4-byte dictionary id, then returns Z_NEED_DICT, which .NET raises as an IOException
	// that PAKLib wraps; with the id cut short the input simply ends
	const uint8_t dict[] = { 0x78, 0xBB, 0, 0, 0, 0 };
	CHECK_THROWS_AS(inflate(dict, sizeof(dict), 10), invalid_data_error,
		"PNG image data cannot be decompressed. The underlying compression routine returned an unexpected error code.");
	CHECK(inflate(dict, 5, 10).empty());
	// Fixed block: a match at distance 1 with no output yet
	const uint8_t far_back[] = { 0x78, 0x01, 0x03, 0x02 };                // BFINAL, fixed, length 257 (code 0000001), dist code 0
	CHECK_THROWS_AS(inflate(far_back, sizeof(far_back), 10), invalid_data_error, data_error);
}

TEST_CASE("Deflate: output is valid zlib with a correct Adler-32 and round-trips") {
	std::vector<bytes> inputs;
	inputs.push_back(bytes());
	inputs.push_back(bytes(1, 42));
	inputs.push_back(pattern_a());
	inputs.push_back(pattern_b(300000));         // several blocks
	inputs.push_back(bytes(1 << 20, 0));          // long runs
	bytes noise(70000);
	uint32_t state = 12345;
	for (uint8_t& v : noise) {
		state = state * 1103515245u + 12345u;
		v = static_cast<uint8_t>(state >> 16);
	}
	inputs.push_back(noise);                      // incompressible: stored blocks
	for (const bytes& raw : inputs) {
		const bytes z = PAKLib::detail::zlib_deflate(raw);
		REQUIRE(z.size() >= 6);
		CHECK((z[0] & 0x0F) == 8);
		CHECK(((z[0] << 8) | z[1]) % 31 == 0);
		CHECK((z[1] & 0x20) == 0);
		CHECK(be32(z, z.size() - 4) == adler(raw));
		CHECK(inflate(z.data(), z.size(), raw.size() + 10) == raw);
	}
	CHECK(PAKLib::detail::zlib_deflate(bytes(1 << 20, 0)).size() < 5000);
	CHECK(PAKLib::detail::zlib_deflate(pattern_b(300000)).size() < 300000 / 4);
	CHECK(PAKLib::detail::zlib_deflate(noise).size() < noise.size() + 64);
}

// ---- Encoding and conversion ----

TEST_CASE("to_png: output is 8-bit RGBA, filter 0, IHDR/IDAT/IEND with correct CRCs") {
	const bytes rgba = test_rgba(6, 5);
	const bytes png = si::to_png(fixture::make_rgba_png(6, 5, rgba, 5));
	REQUIRE(png.size() > 8 + 25 + 12 + 12);
	CHECK(si::get_format(png) == sprite_image_format::png);
	CHECK(be32(png, 8) == 13);
	CHECK(std::memcmp(&png[12], "IHDR", 4) == 0);
	CHECK(be32(png, 16) == 6);
	CHECK(be32(png, 20) == 5);
	CHECK(png[24] == 8);
	CHECK(png[25] == 6);
	CHECK(png[26] == 0);
	CHECK(png[27] == 0);
	CHECK(png[28] == 0);
	size_t at = 8;
	std::vector<std::string> types;
	while (at + 12 <= png.size()) {
		const uint32_t length = be32(png, at);
		types.push_back(std::string(reinterpret_cast<const char*>(&png[at + 4]), 4));
		CHECK(be32(png, at + 8 + length) == fixture::crc32(&png[at + 4], 4 + length));
		if (types.back() == "IDAT") {
			const bytes raw = inflate(&png[at + 8], length, 1000);
			REQUIRE(raw.size() == 5 * (6 * 4 + 1));
			for (size_t y = 0; y < 5; ++y)
				CHECK(raw[y * 25] == 0);
		}
		at += 12 + length;
	}
	CHECK(at == png.size());
	CHECK((types == std::vector<std::string>{ "IHDR", "IDAT", "IEND" }));
	CHECK(decode(png).rgba == rgba);
}

TEST_CASE("to_png: a BMP's colour-key pixels become transparent, top-left when there is no key") {
	const std::vector<uint32_t> rgb = { 0xFF00FF, 0x123456, 0x123456, 0xFF00FF, 0x000000, 0x123457 };
	fixture::bmp_spec s;
	s.width = 3;
	s.height = 2;
	const bytes bmp = fixture::make_bmp_custom(s, bmp24_rows(3, 2, rgb));

	const PAKLib::detail::image keyed = decode(si::to_png(bmp, 0xFF123456u));
	const uint8_t keyed_alpha[6] = { 255, 0, 0, 255, 255, 255 };
	for (int i = 0; i < 6; ++i)
		CHECK(keyed.rgba[static_cast<size_t>(i) * 4 + 3] == keyed_alpha[i]);
	CHECK(fixture::rgba_at(keyed, 1, 0) == 0x12345600u);   // colour kept, only alpha cleared

	const PAKLib::detail::image top_left = decode(si::to_png(bmp));
	const uint8_t tl_alpha[6] = { 0, 255, 255, 0, 255, 255 };
	for (int i = 0; i < 6; ++i)
		CHECK(top_left.rgba[static_cast<size_t>(i) * 4 + 3] == tl_alpha[i]);

	// Only RGB is compared: a key's alpha byte does not matter
	CHECK(decode(si::to_png(bmp, 0x01123456u)).rgba == keyed.rgba);
}

TEST_CASE("to_png: a PNG keeps its pixels and ignores the colour key") {
	const bytes rgba = test_rgba(4, 4);
	const bytes src = fixture::make_rgba_png(4, 4, rgba);
	CHECK(decode(si::to_png(src)).rgba == rgba);
	CHECK(decode(si::to_png(src, 0xFF000000u | rgba[4] << 16 | rgba[5] << 8 | rgba[6])).rgba == rgba);
}

TEST_CASE("to_bmp24: header fields, bottom-up rows, padding and alpha below 128 filled with the key") {
	const bytes rgba = test_rgba(5, 3);
	const bytes png = fixture::make_rgba_png(5, 3, rgba);
	const bytes bmp = si::to_bmp24(png, 0xFF0A0B0Cu);
	const size_t row = (5 * 3 + 3) & ~static_cast<size_t>(3);
	REQUIRE(bmp.size() == 54 + row * 3);
	CHECK(bmp[0] == 'B');
	CHECK(bmp[1] == 'M');
	CHECK(le32(bmp, 2) == bmp.size());
	CHECK(le32(bmp, 6) == 0);
	CHECK(le32(bmp, 10) == 54);
	CHECK(le32(bmp, 14) == 40);
	CHECK(le32(bmp, 18) == 5);
	CHECK(le32(bmp, 22) == 3);
	CHECK((bmp[26] | (bmp[27] << 8)) == 1);
	CHECK((bmp[28] | (bmp[29] << 8)) == 24);
	CHECK(le32(bmp, 30) == 0);
	CHECK(le32(bmp, 34) == row * 3);
	for (size_t i = 38; i < 54; ++i)
		CHECK(bmp[i] == 0);
	for (uint32_t y = 0; y < 3; ++y)
		for (uint32_t x = 0; x < 5; ++x) {
			const size_t src = (y * 5 + x) * 4;
			const size_t dst = 54 + (2 - y) * row + x * 3;
			const bool transparent = rgba[src + 3] < 128;
			CHECK(bmp[dst] == (transparent ? 0x0C : rgba[src + 2]));
			CHECK(bmp[dst + 1] == (transparent ? 0x0B : rgba[src + 1]));
			CHECK(bmp[dst + 2] == (transparent ? 0x0A : rgba[src]));
		}
	for (size_t y = 0; y < 3; ++y)
		CHECK(bmp[54 + y * row + 15] == 0);

	// No key: magenta
	const bytes magenta = si::to_bmp24(png);
	CHECK(magenta[54 + 2 * row] == 0xFF);
	CHECK(magenta[54 + 2 * row + 1] == 0x00);
	CHECK(magenta[54 + 2 * row + 2] == 0xFF);
}

TEST_CASE("to_bmp24: other BMP depths convert pixel for pixel; 24bpp re-encodes identically") {
	fixture::bmp_spec s8;
	s8.width = 3;
	s8.height = 2;
	s8.bpp = 8;
	s8.palette = { 0x112233, 0x445566 };
	const bytes bmp8 = fixture::make_bmp_custom(s8, bytes{ 0, 1, 0, 0, 1, 1, 0, 0 });
	const bytes bmp24 = si::to_bmp24(bmp8, 0xFF445566u);   // BMP alpha is always 255: the key is not applied
	CHECK(si::get_bmp_bits_per_pixel(bmp24) == 24);
	CHECK(decode(bmp24).rgba == decode(bmp8).rgba);
	CHECK(si::to_bmp24(bmp24) == bmp24);
	CHECK(decode(si::to_bmp24(fixture::make_bmp(3, 3, 1, 2, 3))).rgba == decode(fixture::make_bmp(3, 3, 1, 2, 3)).rgba);
	CHECK_THROWS_AS(si::to_bmp24(bytes{ 0, 1 }), invalid_data_error, "Image data is neither BMP nor PNG.");
	CHECK_THROWS_AS(si::to_png(bytes{ 0, 1 }), invalid_data_error, "Image data is neither BMP nor PNG.");
}

TEST_CASE("Sprite: convert_to_bmp gives a PNG without a key magenta, keeps an existing key") {
	const bytes rgba = test_rgba(4, 3);
	PAKLib::sprite s;
	s.image_data = fixture::make_rgba_png(4, 3, rgba);
	s.convert_to_bmp();
	CHECK(s.get_color_key() == 0xFFFF00FFu);
	CHECK(s.image_data == si::to_bmp24(fixture::make_rgba_png(4, 3, rgba), 0xFFFF00FFu));

	PAKLib::sprite keyed;
	keyed.set_color_key(1, 2, 3);
	keyed.image_data = fixture::make_rgba_png(4, 3, rgba);
	keyed.convert_to_bmp();
	CHECK(keyed.get_color_key() == 0xFF010203u);
	CHECK(keyed.image_data == si::to_bmp24(fixture::make_rgba_png(4, 3, rgba), 0xFF010203u));

	// A BMP without a key stays without one
	fixture::bmp_spec s8;
	s8.width = 2;
	s8.height = 1;
	s8.bpp = 8;
	s8.palette = { 0xABCDEF };
	PAKLib::sprite bmp;
	bmp.image_data = fixture::make_bmp_custom(s8, bytes(4, 0));
	bmp.convert_to_bmp();
	CHECK(!bmp.has_color_key());
	CHECK(si::get_bmp_bits_per_pixel(bmp.image_data) == 24);
	CHECK(fixture::rgba_at(decode(bmp.image_data), 1, 0) == opaque(0xABCDEF));
}

TEST_CASE("Sprite: convert_to_png takes the top-left colour as the key, and converting back restores the BMP") {
	const std::vector<uint32_t> rgb = { 0x28 << 16 | 0x28 << 8 | 0x31, 0x112233, 0x445566, 0x28 << 16 | 0x28 << 8 | 0x31 };
	fixture::bmp_spec s;
	s.width = 2;
	s.height = 2;
	const bytes original = fixture::make_bmp_custom(s, bmp24_rows(2, 2, rgb));
	PAKLib::sprite sp;
	sp.image_data = original;
	sp.convert_to_png();
	CHECK(sp.image_format() == sprite_image_format::png);
	CHECK(sp.get_color_key() == 0xFF282831u);
	const PAKLib::detail::image img = decode(sp.image_data);
	CHECK(fixture::rgba_at(img, 0, 0) == 0x28283100u);
	CHECK(fixture::rgba_at(img, 1, 0) == 0x112233FFu);
	sp.convert_to_bmp();
	CHECK(decode(sp.image_data).rgba == decode(original).rgba);
	CHECK(sp.get_color_key() == 0xFF282831u);

	// An existing key is used and kept
	PAKLib::sprite keyed;
	keyed.set_color_key(0x11, 0x22, 0x33);
	keyed.image_data = original;
	keyed.convert_to_png();
	CHECK(keyed.get_color_key() == 0xFF112233u);
	CHECK(fixture::rgba_at(decode(keyed.image_data), 1, 0) == 0x11223300u);
	CHECK(fixture::rgba_at(decode(keyed.image_data), 0, 0) == 0x282831FFu);

	// A PNG gets no key
	PAKLib::sprite png;
	png.image_data = fixture::make_rgba_png(2, 2, test_rgba(2, 2));
	png.convert_to_png();
	CHECK(!png.has_color_key());
	CHECK(decode(png.image_data).rgba == test_rgba(2, 2));
}

TEST_CASE("Sprite: a failed conversion leaves the image unchanged") {
	PAKLib::sprite s;
	s.image_data = bytes{ 1, 2, 3, 4 };
	CHECK_THROWS_AS(s.convert_to_png(), invalid_data_error, "Image data is neither BMP nor PNG.");
	CHECK_THROWS_AS(s.convert_to_bmp(), invalid_data_error, "Image data is neither BMP nor PNG.");
	CHECK((s.image_data == bytes{ 1, 2, 3, 4 }));
	CHECK(!s.has_color_key());
}

TEST_CASE("BMP: a DIB size whose palette offset overflows int is invalid data, as PAKLib's IndexOutOfRange") {
	// 8 bpp, 4x4, BI_RGB, no palette or pixels present (c_bmp8_dibhuge.img / m0065.img from the review corpus).
	// SpriteImage computes 14 + dibSize in int, so from 0x7FFFFFF2 the palette offset wraps negative.
	bytes bmp(70, 0);
	bmp[0] = 'B';
	bmp[1] = 'M';
	fixture::set_u32(bmp, 2, 70);
	fixture::set_u32(bmp, 10, 54);
	fixture::set_u32(bmp, 18, 4);
	fixture::set_u32(bmp, 22, 4);
	bmp[26] = 1;
	bmp[28] = 8;
	bmp[34] = 16;
	for (const uint32_t dib : { 0x7FFFFFF2u, 0x7FFFFFFFu }) {
		fixture::set_u32(bmp, 14, dib);
		const std::string message = "BMP header size " + std::to_string(dib) + " puts the palette outside the data.";
		CHECK_THROWS_AS(decode(bmp), invalid_data_error, message);
		CHECK_THROWS_AS(PAKLib::sprite_image::to_bmp24(bmp), invalid_data_error, message);
		CHECK_THROWS_AS(PAKLib::sprite_image::to_png(bmp), invalid_data_error, message);
	}
	// One below: no overflow in C# either; the palette lies past the data and reads as black
	fixture::set_u32(bmp, 14, 0x7FFFFFF1u);
	const PAKLib::detail::image img = decode(bmp);
	CHECK(img.width == 4);
	CHECK(img.height == 4);
	CHECK(img.rgba[0] == 0);
	CHECK(img.rgba[3] == 255);
}
