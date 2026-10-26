#pragma once
#include "PAK.h"
#include "TestFramework.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
#endif

// Builds PAK files byte by byte, independently of PAK.h, so loaders are tested against the format
// as documented (PAKLib README) rather than against themselves.
namespace fixture {
	using bytes = std::vector<uint8_t>;

	inline void put_u16(bytes& out, uint16_t v) {
		out.push_back(static_cast<uint8_t>(v));
		out.push_back(static_cast<uint8_t>(v >> 8));
	}

	inline void put_u32(bytes& out, uint32_t v) {
		for (int i = 0; i < 4; ++i)
			out.push_back(static_cast<uint8_t>(v >> (8 * i)));
	}

	inline void set_u32(bytes& out, size_t at, uint32_t v) {
		for (int i = 0; i < 4; ++i)
			out[at + i] = static_cast<uint8_t>(v >> (8 * i));
	}

	inline void put_u32_be(bytes& out, uint32_t v) {
		for (int i = 3; i >= 0; --i)
			out.push_back(static_cast<uint8_t>(v >> (8 * i)));
	}

	inline void put_text(bytes& out, const char* text, size_t length) {
		out.insert(out.end(), text, text + length);
	}

	inline uint32_t crc32(const uint8_t* data, size_t length) {
		uint32_t crc = 0xFFFFFFFFu;
		for (size_t i = 0; i < length; ++i) {
			crc ^= data[i];
			for (int k = 0; k < 8; ++k)
				crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
		}
		return ~crc;
	}

	// 24bpp bottom-up BMP, every pixel set to (r, g, b)
	inline bytes make_bmp(uint32_t width, uint32_t height, uint8_t r = 0, uint8_t g = 0, uint8_t b = 0) {
		const uint32_t row = (width * 3 + 3) & ~3u;
		const uint32_t pixels = row * height;
		bytes out;
		put_text(out, "BM", 2);
		put_u32(out, 54 + pixels);
		put_u32(out, 0);
		put_u32(out, 54);
		put_u32(out, 40);
		put_u32(out, width);
		put_u32(out, height);
		put_u16(out, 1);
		put_u16(out, 24);
		put_u32(out, 0);
		put_u32(out, pixels);
		put_u32(out, 2835);
		put_u32(out, 2835);
		put_u32(out, 0);
		put_u32(out, 0);
		for (uint32_t y = 0; y < height; ++y) {
			for (uint32_t x = 0; x < width; ++x) {
				out.push_back(b);
				out.push_back(g);
				out.push_back(r);
			}
			for (uint32_t p = width * 3; p < row; ++p)
				out.push_back(0);
		}
		return out;
	}

	inline void put_png_chunk(bytes& out, const char* type, const bytes& body) {
		put_u32_be(out, static_cast<uint32_t>(body.size()));
		const size_t type_at = out.size();
		put_text(out, type, 4);
		out.insert(out.end(), body.begin(), body.end());
		put_u32_be(out, crc32(out.data() + type_at, 4 + body.size()));
	}

	// Signature + IHDR (8-bit RGBA) + IEND; enough for header-level checks, no pixel data
	inline bytes make_png_header(uint32_t width, uint32_t height) {
		static const uint8_t signature[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
		bytes out(signature, signature + 8);
		bytes ihdr;
		put_u32_be(ihdr, width);
		put_u32_be(ihdr, height);
		ihdr.push_back(8);
		ihdr.push_back(6);
		ihdr.push_back(0);
		ihdr.push_back(0);
		ihdr.push_back(0);
		put_png_chunk(out, "IHDR", ihdr);
		put_png_chunk(out, "IEND", bytes{});
		return out;
	}

	struct rect {
		uint16_t x, y, width, height;
		int16_t pivot_x, pivot_y;
	};

	struct sprite_spec {
		std::vector<rect> rects{ { 0, 0, 1, 1, 0, 0 } };
		uint8_t color_key[4]{ 0, 0, 0, 0 }; // on-disk order A RR GG BB
		bytes image = make_bmp(1, 1);
		bool valid_magic = true;
	};

	struct pak_layout {
		bytes data;
		std::vector<size_t> sprite_offsets;  // where each sprite entry starts
		std::vector<size_t> table_offsets;   // where each (offset, size) table record starts
		std::vector<size_t> rect_count_offsets;
		std::vector<size_t> color_key_offsets;
		size_t count_offset = 20;
	};

	inline pak_layout build_pak(const std::vector<sprite_spec>& sprites) {
		pak_layout pak;
		bytes& out = pak.data;
		put_text(out, "<Pak file header>", 17);
		out.insert(out.end(), 3, 0);
		put_u32(out, static_cast<uint32_t>(sprites.size()));
		for (size_t i = 0; i < sprites.size(); ++i) {
			pak.table_offsets.push_back(out.size());
			put_u32(out, 0);
			put_u32(out, 0);
		}
		for (size_t i = 0; i < sprites.size(); ++i) {
			const sprite_spec& s = sprites[i];
			const size_t start = out.size();
			pak.sprite_offsets.push_back(start);
			if (s.valid_magic)
				put_text(out, "<Sprite File Header>", 20);
			else
				out.insert(out.end(), 20, 0);
			out.insert(out.end(), 80, 0);
			pak.rect_count_offsets.push_back(out.size());
			put_u32(out, static_cast<uint32_t>(s.rects.size()));
			for (const rect& r : s.rects) {
				put_u16(out, r.x);
				put_u16(out, r.y);
				put_u16(out, r.width);
				put_u16(out, r.height);
				put_u16(out, static_cast<uint16_t>(r.pivot_x));
				put_u16(out, static_cast<uint16_t>(r.pivot_y));
			}
			pak.color_key_offsets.push_back(out.size());
			out.insert(out.end(), s.color_key, s.color_key + 4);
			out.insert(out.end(), s.image.begin(), s.image.end());
			set_u32(out, pak.table_offsets[i], static_cast<uint32_t>(start));
			set_u32(out, pak.table_offsets[i] + 4, static_cast<uint32_t>(out.size() - start));
		}
		return pak;
	}

	inline std::vector<sprite_spec> sprites(size_t count) {
		return std::vector<sprite_spec>(count);
	}

	inline void make_dir(const std::string& path) {
#if defined(_WIN32)
		_mkdir(path.c_str());
#else
		mkdir(path.c_str(), 0755);
#endif
	}

	inline std::string output_dir() {
		const std::string dir = test::data_dir() + "/TestOutput";
		make_dir(dir);
		return dir;
	}

	// Writes data to <data_dir>/TestOutput/<name> and returns that path
	inline std::string write(const std::string& name, const bytes& data) {
		const std::string path = output_dir() + "/" + name;
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
		if (!out)
			throw std::runtime_error("fixture: cannot write " + path);
		return path;
	}

	inline bytes read(const std::string& path) {
		std::ifstream in(path, std::ios::binary);
		if (!in)
			throw std::runtime_error("fixture: cannot read " + path);
		return bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	}

	inline std::string absolute(const std::string& path) {
#if defined(_WIN32)
		char buffer[4096];
		if (_fullpath(buffer, path.c_str(), sizeof(buffer)) == nullptr)
			throw std::runtime_error("fixture: cannot resolve " + path);
		return buffer;
#else
		char* resolved = realpath(path.c_str(), nullptr);
		if (resolved == nullptr)
			throw std::runtime_error("fixture: cannot resolve " + path);
		std::string result(resolved);
		std::free(resolved);
		return result;
#endif
	}

	inline bool is_absolute(const std::string& path) {
		if (!path.empty() && (path[0] == '/' || path[0] == '\\'))
			return true;
		return path.size() > 1 && path[1] == ':';
	}

	inline std::string amp_pak() {
		return test::data_dir() + "/Amp.pak";
	}

	// Every loader, so a scenario can run against all of them
	struct full_loader {
		const char* name;
		PAKLib::pak(*load)(const std::string&);
	};
	struct sprite_loader {
		const char* name;
		PAKLib::sprite(*load)(const std::string&, size_t);
	};

	inline const std::vector<full_loader>& full_loaders() {
		static const std::vector<full_loader> loaders = {
			{ "loadpak", &PAKLib::loadpak },
			{ "loadpak_fast", &PAKLib::loadpak_fast },
		};
		return loaders;
	}

	inline const std::vector<sprite_loader>& sprite_loaders() {
		static const std::vector<sprite_loader> loaders = {
			{ "get_sprite", &PAKLib::get_sprite },
			{ "get_sprite_fast", &PAKLib::get_sprite_fast },
		};
		return loaders;
	}

	inline PAKLib::pak read_in_memory(const std::string& path) {
		return PAKLib::pak::read(read(path));
	}

	// The PAKLib 2.0 readers: loadpak (PAK.ReadFromFile) and pak::read (PAKData.Read) on the file's bytes
	inline const std::vector<full_loader>& strict_loaders() {
		static const std::vector<full_loader> loaders = {
			{ "loadpak", &PAKLib::loadpak },
			{ "pak::read", &read_in_memory },
		};
		return loaders;
	}

	// ---- Independent image builders (no PAK.h code), for decoder tests ----

	// zlib stream of stored (uncompressed) deflate blocks
	inline bytes zlib_stored(const bytes& raw, size_t block = 65535) {
		bytes out = { 0x78, 0x01 };
		size_t at = 0;
		do {
			const size_t n = std::min(block, raw.size() - at);
			const bool last = at + n == raw.size();
			out.push_back(last ? 1 : 0);
			put_u16(out, static_cast<uint16_t>(n));
			put_u16(out, static_cast<uint16_t>(~n));
			out.insert(out.end(), raw.begin() + static_cast<std::ptrdiff_t>(at), raw.begin() + static_cast<std::ptrdiff_t>(at + n));
			at += n;
		} while (at < raw.size());
		uint32_t a = 1, b = 0;
		for (uint8_t v : raw) {
			a = (a + v) % 65521;
			b = (b + a) % 65521;
		}
		put_u32_be(out, (b << 16) | a);
		return out;
	}

	struct png_spec {
		uint32_t width = 1, height = 1;
		uint8_t bit_depth = 8, color_type = 6, interlace = 0;
		bytes plte, trns;
	};

	inline int png_channels(uint8_t color_type) {
		switch (color_type) {
		case 0: return 1;
		case 2: return 3;
		case 3: return 1;
		case 4: return 2;
		default: return 4;
		}
	}

	inline int png_paeth(int a, int b, int c) {
		const int p = a + b - c;
		const int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
		return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
	}

	// Filtered scanlines for every pass; sample(x, y, channel) gives the raw sample value.
	// filter: 0-4 for every row, or 5 to cycle 0,1,2,3,4 row by row.
	template <typename Sample>
	inline bytes png_scanlines(const png_spec& s, Sample sample, int filter = 0) {
		static const int adam7[7][4] = { { 0, 0, 8, 8 }, { 4, 0, 8, 8 }, { 0, 4, 4, 8 }, { 2, 0, 4, 4 }, { 0, 2, 2, 4 }, { 1, 0, 2, 2 }, { 0, 1, 1, 2 } };
		static const int single[1][4] = { { 0, 0, 1, 1 } };
		const int(*passes)[4] = s.interlace == 1 ? adam7 : single;
		const int pass_count = s.interlace == 1 ? 7 : 1;
		const int channels = png_channels(s.color_type);
		const int bits = channels * s.bit_depth;
		const size_t step = static_cast<size_t>(std::max(1, bits / 8));
		bytes out;
		int row_number = 0;
		for (int p = 0; p < pass_count; ++p) {
			const int pw = (static_cast<int>(s.width) - passes[p][0] + passes[p][2] - 1) / passes[p][2];
			const int ph = (static_cast<int>(s.height) - passes[p][1] + passes[p][3] - 1) / passes[p][3];
			if (pw <= 0 || ph <= 0)
				continue;
			const size_t row_bytes = static_cast<size_t>((pw * bits + 7) / 8);
			bytes previous(row_bytes, 0);
			for (int row = 0; row < ph; ++row) {
				bytes raw(row_bytes, 0);
				const uint32_t y = static_cast<uint32_t>(passes[p][1] + row * passes[p][3]);
				size_t bit = 0;
				for (int column = 0; column < pw; ++column) {
					const uint32_t x = static_cast<uint32_t>(passes[p][0] + column * passes[p][2]);
					for (int c = 0; c < channels; ++c) {
						const uint32_t v = sample(x, y, c);
						if (s.bit_depth == 16) {
							raw[bit / 8] = static_cast<uint8_t>(v >> 8);
							raw[bit / 8 + 1] = static_cast<uint8_t>(v);
						}
						else if (s.bit_depth == 8) {
							raw[bit / 8] = static_cast<uint8_t>(v);
						}
						else {
							raw[bit / 8] |= static_cast<uint8_t>(v << (8 - s.bit_depth - static_cast<int>(bit % 8)));
						}
						bit += s.bit_depth;
					}
				}
				const int f = filter == 5 ? row_number % 5 : filter;
				out.push_back(static_cast<uint8_t>(f));
				for (size_t i = 0; i < row_bytes; ++i) {
					const int left = i >= step ? raw[i - step] : 0;
					const int up = previous[i];
					const int up_left = i >= step ? previous[i - step] : 0;
					int predictor = 0;
					if (f == 1) predictor = left;
					else if (f == 2) predictor = up;
					else if (f == 3) predictor = (left + up) / 2;
					else if (f == 4) predictor = png_paeth(left, up, up_left);
					out.push_back(static_cast<uint8_t>(raw[i] - predictor));
				}
				previous = raw;
				++row_number;
			}
		}
		return out;
	}

	// Signature, IHDR, PLTE and tRNS when given, the zlib data split over idat_chunks IDAT chunks, IEND
	inline bytes make_png(const png_spec& s, const bytes& zlib_data, size_t idat_chunks = 1) {
		static const uint8_t signature[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
		bytes out(signature, signature + 8);
		bytes ihdr;
		put_u32_be(ihdr, s.width);
		put_u32_be(ihdr, s.height);
		ihdr.push_back(s.bit_depth);
		ihdr.push_back(s.color_type);
		ihdr.push_back(0);
		ihdr.push_back(0);
		ihdr.push_back(s.interlace);
		put_png_chunk(out, "IHDR", ihdr);
		if (!s.plte.empty())
			put_png_chunk(out, "PLTE", s.plte);
		if (!s.trns.empty())
			put_png_chunk(out, "tRNS", s.trns);
		const size_t part = (zlib_data.size() + idat_chunks - 1) / idat_chunks;
		for (size_t at = 0; at < zlib_data.size(); at += part) {
			const size_t n = std::min(part, zlib_data.size() - at);
			put_png_chunk(out, "IDAT", bytes(zlib_data.begin() + static_cast<std::ptrdiff_t>(at), zlib_data.begin() + static_cast<std::ptrdiff_t>(at + n)));
		}
		put_png_chunk(out, "IEND", bytes{});
		return out;
	}

	// 8-bit RGBA PNG from top-down RGBA pixels
	inline bytes make_rgba_png(uint32_t width, uint32_t height, const bytes& rgba, int filter = 0) {
		png_spec s;
		s.width = width;
		s.height = height;
		const bytes lines = png_scanlines(s, [&](uint32_t x, uint32_t y, int c) { return static_cast<uint32_t>(rgba[(y * width + x) * 4 + static_cast<uint32_t>(c)]); }, filter);
		return make_png(s, zlib_stored(lines));
	}

	struct bmp_spec {
		int32_t width = 1, height = 1;
		uint16_t bpp = 24;
		uint32_t compression = 0;
		int32_t colors_used = 0;
		std::vector<uint32_t> palette;   // 0xRRGGBB
		bool masks = false;              // write 3 masks after the 40-byte header
		uint32_t mask_r = 0, mask_g = 0, mask_b = 0;
	};

	// BMP with a 40-byte DIB header: header, optional masks, palette (B G R 0), then the given pixel bytes
	inline bytes make_bmp_custom(const bmp_spec& s, const bytes& pixels) {
		const uint32_t extra = s.masks ? 12u : 0u;
		const uint32_t offset = 54 + extra + static_cast<uint32_t>(s.palette.size()) * 4;
		bytes out;
		put_text(out, "BM", 2);
		put_u32(out, offset + static_cast<uint32_t>(pixels.size()));
		put_u32(out, 0);
		put_u32(out, offset);
		put_u32(out, 40);
		put_u32(out, static_cast<uint32_t>(s.width));
		put_u32(out, static_cast<uint32_t>(s.height));
		put_u16(out, 1);
		put_u16(out, s.bpp);
		put_u32(out, s.compression);
		put_u32(out, static_cast<uint32_t>(pixels.size()));
		put_u32(out, 2835);
		put_u32(out, 2835);
		put_u32(out, static_cast<uint32_t>(s.colors_used));
		put_u32(out, 0);
		if (s.masks) {
			put_u32(out, s.mask_r);
			put_u32(out, s.mask_g);
			put_u32(out, s.mask_b);
		}
		for (uint32_t c : s.palette) {
			out.push_back(static_cast<uint8_t>(c));
			out.push_back(static_cast<uint8_t>(c >> 8));
			out.push_back(static_cast<uint8_t>(c >> 16));
			out.push_back(0);
		}
		out.insert(out.end(), pixels.begin(), pixels.end());
		return out;
	}

	// RGBA of pixel (x, y) in a decoded image, as 0xRRGGBBAA
	inline uint32_t rgba_at(const PAKLib::detail::image& img, int x, int y) {
		const size_t o = (static_cast<size_t>(y) * static_cast<size_t>(img.width) + static_cast<size_t>(x)) * 4;
		return (static_cast<uint32_t>(img.rgba[o]) << 24) | (static_cast<uint32_t>(img.rgba[o + 1]) << 16) |
			(static_cast<uint32_t>(img.rgba[o + 2]) << 8) | img.rgba[o + 3];
	}

	inline bool same_rects(const std::vector<PAKLib::sprite_rect>& a, const std::vector<PAKLib::sprite_rect>& b) {
		return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(PAKLib::sprite_rect)) == 0);
	}

	inline bool same_sprite(const PAKLib::sprite& a, const PAKLib::sprite& b) {
		return same_rects(a.sprite_rectangles, b.sprite_rectangles) && a.image_data == b.image_data &&
			a.get_color_key() == b.get_color_key();
	}
}
