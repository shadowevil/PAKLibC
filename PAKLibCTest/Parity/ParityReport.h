// C++ twin of harness\Program.cs: the same report, line for line, produced from PAK.h.
// Used by the parity replay (TestsParity.cpp). Compiles as C++14 and C++17.
#pragma once
#include "PAK.h"   // PAKLibC\ is on the include path, as for PakFixture.h
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace parity {
	using bytes = std::vector<uint8_t>;

	inline uint64_t fnv(const bytes& d) {
		uint64_t h = 14695981039346656037ULL;
		for (uint8_t b : d) {
			h ^= b;
			h *= 1099511628211ULL;
		}
		return h;
	}

	inline std::string hex(uint64_t h) {
		char buffer[32];
		std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(h));
		return buffer;
	}

	inline std::string key(uint32_t k) {
		if (k == 0)
			return "none";
		char buffer[16];
		std::snprintf(buffer, sizeof(buffer), "%08X", k);
		return buffer;
	}

	// The .NET exception type each PAKLibC exception stands for
	inline std::string type_name(const std::exception& e) {
		if (dynamic_cast<const PAKLib::invalid_data_error*>(&e) != nullptr)
			return "InvalidDataException";
		if (dynamic_cast<const PAKLib::file_not_found_error*>(&e) != nullptr)
			return "FileNotFoundException";
		if (dynamic_cast<const PAKLib::not_supported_error*>(&e) != nullptr)
			return "NotSupportedException";
		if (dynamic_cast<const PAKLib::argument_out_of_range_error*>(&e) != nullptr)
			return "ArgumentOutOfRangeException";
		if (dynamic_cast<const std::invalid_argument*>(&e) != nullptr)
			return "ArgumentException";
		return "Exception";
	}

	inline std::string err(const std::exception& e) {
		return type_name(e) + ": " + e.what();
	}

	inline std::string sprite_line(const PAKLib::sprite& s) {
		bytes rects;
		for (const PAKLib::sprite_rect& r : s.sprite_rectangles) {
			PAKLib::detail::put_u16le(rects, r.x);
			PAKLib::detail::put_u16le(rects, r.y);
			PAKLib::detail::put_u16le(rects, r.width);
			PAKLib::detail::put_u16le(rects, r.height);
			PAKLib::detail::put_u16le(rects, static_cast<uint16_t>(r.pivotX));
			PAKLib::detail::put_u16le(rects, static_cast<uint16_t>(r.pivotY));
		}
		return "r=" + std::to_string(s.sprite_rectangles.size()) + " rh=" + hex(fnv(rects)) + " k=" + key(s.get_color_key()) +
			" len=" + std::to_string(s.image_data.size()) + " h=" + hex(fnv(s.image_data));
	}

	inline std::string pixels(const bytes& data) {
		try {
			const PAKLib::detail::image im = PAKLib::detail::decode(data);
			return "px " + std::to_string(im.width) + "x" + std::to_string(im.height) + " " + hex(fnv(im.rgba));
		}
		catch (const std::exception& e) {
			return "px ERR " + err(e);
		}
	}

	inline void to_bytes_line(std::ostringstream& sb, const PAKLib::pak& d, const char* indent) {
		try {
			const bytes t = d.to_bytes();
			sb << indent << "tobytes " << hex(fnv(t)) << " " << t.size() << "\n";
		}
		catch (const std::exception& e) {
			sb << indent << "tobytes ERR " << err(e) << "\n";
		}
	}

	template <typename Run>
	inline void repair(std::ostringstream& sb, const std::string& label, Run run) {
		try {
			const PAKLib::repair_report r = run();
			sb << label << " success=" << (r.success() ? 1 : 0) << " placeholders=";
			for (size_t i = 0; i < r.placeholder_sprites.size(); ++i)
				sb << (i ? "," : "") << r.placeholder_sprites[i];
			sb << "\n";
			for (const PAKLib::repair_issue& i : r.issues)
				sb << "  " << i.to_string() << "\n";
			for (const PAKLib::sprite& s : r.data.sprites)
				sb << "  " << sprite_line(s) << "\n";
			to_bytes_line(sb, r.data, "  ");
		}
		catch (const std::exception& e) {
			sb << label << " ERR " << err(e) << "\n";
		}
	}

	inline void pak(std::ostringstream& sb, const bytes& data) {
		try {
			const PAKLib::pak d = PAKLib::pak::read(data);
			sb << "read OK n=" << d.sprites.size() << "\n";
			for (const PAKLib::sprite& s : d.sprites)
				sb << "  " << sprite_line(s) << " " << pixels(s.image_data) << "\n";
			to_bytes_line(sb, d, "");
		}
		catch (const std::exception& e) {
			sb << "read ERR " << err(e) << "\n";
		}
		repair(sb, "repair whole force=0", [&] { return PAKLib::pak_repair::repair(data, false); });
		repair(sb, "repair whole force=1", [&] { return PAKLib::pak_repair::repair(data, true); });
		const int64_t n = static_cast<int64_t>(data.size());
		int64_t te = 24;
		if (n >= 24) {
			const int32_t c = PAKLib::detail::i32le(&data[20]);
			if (c >= 0 && 24 + static_cast<int64_t>(c) * 8 <= n)
				te = 24 + static_cast<int64_t>(c) * 8;
		}
		if (te > n)
			te = n;
		const int64_t two = 2 * n / 3 < te ? te : 2 * n / 3;
		const int64_t windows[4][2] = { { te, n }, { n / 3, n }, { te, two }, { n / 5, 4 * n / 5 } };
		for (const auto& w : windows) {
			const int64_t from = w[0], to = w[1];
			repair(sb, "repair section " + std::to_string(from) + "-" + std::to_string(to) + " force=1",
				[&] { return PAKLib::pak_repair::repair(data, from, to, true); });
		}
	}

	inline const char* format_name(PAKLib::sprite_image_format f) {
		switch (f) {
		case PAKLib::sprite_image_format::bmp: return "Bmp";
		case PAKLib::sprite_image_format::png: return "Png";
		default: return "Unknown";
		}
	}

	inline void img(std::ostringstream& sb, const bytes& data) {
		namespace si = PAKLib::sprite_image;
		sb << "format " << format_name(si::get_format(data)) << " bpp=" << si::get_bmp_bits_per_pixel(data) << "\n";
		int w = 0, h = 0;
		const bool ok = si::try_get_size(data, w, h);
		sb << "size " << (ok ? 1 : 0) << " " << w << "x" << h << "\n";
		try {
			si::check_size(data);
			sb << "check OK\n";
		}
		catch (const std::exception& e) {
			sb << "check ERR " << err(e) << "\n";
		}
		sb << pixels(data) << "\n";
		const uint32_t bmp_keys[3] = { 0, 0xFF123456u, 0xFFFF00FFu };
		for (uint32_t k : bmp_keys) {
			try {
				const bytes b = si::to_bmp24(data, k);
				sb << "tobmp " << key(k) << " " << hex(fnv(b)) << " " << b.size() << "\n";
			}
			catch (const std::exception& e) {
				sb << "tobmp " << key(k) << " ERR " << err(e) << "\n";
			}
		}
		const uint32_t png_keys[2] = { 0, 0xFF000000u };
		for (uint32_t k : png_keys) {
			try {
				uint32_t used = 0;
				const bytes png = PAKLib::detail::to_png(data, k, used);
				const PAKLib::detail::image im = PAKLib::detail::decode(png);
				sb << "topng " << key(k) << " used=" << key(used) << " " << im.width << "x" << im.height << " " << hex(fnv(im.rgba)) << "\n";
			}
			catch (const std::exception& e) {
				sb << "topng " << key(k) << " ERR " << err(e) << "\n";
			}
		}
	}

	// Control characters (from echoed PNG chunk types) as \xHH, as the harness writes them
	inline std::string escape_controls(const std::string& text) {
		std::string out;
		for (char c : text) {
			const unsigned char u = static_cast<unsigned char>(c);
			if (u < 0x20 && c != '\n') {
				char buffer[8];
				std::snprintf(buffer, sizeof(buffer), "\\x%02X", u);
				out += buffer;
			}
			else {
				out += c;
			}
		}
		return out;
	}

	// One case: "== name" then its lines, exactly as the harness writes them
	inline std::string report(const std::string& name, const bytes& data) {
		std::ostringstream sb;
		sb << "== " << name << "\n";
		if (name.size() > 4 && name.compare(name.size() - 4, 4, ".img") == 0)
			img(sb, data);
		else
			pak(sb, data);
		return escape_controls(sb.str());
	}
}
