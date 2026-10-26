#pragma once
#include "PAK.h"
#include "TestFramework.h"
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

	inline bool same_rects(const std::vector<PAKLib::sprite_rect>& a, const std::vector<PAKLib::sprite_rect>& b) {
		return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(PAKLib::sprite_rect)) == 0);
	}

	inline bool same_sprite(const PAKLib::sprite& a, const PAKLib::sprite& b) {
		return same_rects(a.sprite_rectangles, b.sprite_rectangles) && a.image_data == b.image_data &&
			a.get_color_key() == b.get_color_key();
	}
}
