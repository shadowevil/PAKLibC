#pragma once
// PAKLibC: single-header port of PAKLib 2.0 (C#). Everything is inline, so this header can be included from
// any number of translation units. Standard library only.
// Language: C++17 recommended; C++14 supported. At C++17 the one path resolution (repair_file's "output must
// differ from input" check, detail::full_path) and File.Exists (detail::file_exists) use std::filesystem. At C++14
// they fall back to the C runtime (_wfullpath on Windows; getcwd and stat elsewhere) and the header emits a
// compile-time warning (#pragma message / #warning) to upgrade. GCC 8 needs -lstdc++fs for <filesystem>, and the
// C++14 #warning breaks -Werror builds (add -Wno-error=cpp, or build as C++17).
// Paths are narrow strings, judged (blank, .epak, same file) in the encoding the file functions open them with:
// the ANSI code page on Windows, UTF-8 bytes elsewhere.
// The complete member-by-member map is PAKLibCTest\Parity\api_map.txt, checked by check_api.py.
//
// API map, PAKLib (C#) -> PAKLibC (namespace PAKLib):
//   PAK                                        -> pak (PAK and PAKData are one struct; PAK.Data is the pak itself)
//   PAK.ReadFromFile(path)                     -> loadpak(path)
//   PAK.SaveToFile(pak, path)                  -> save_to_file(pak, path)
//   PAK.RepairFile(in, out, force)             -> repair_file(in, out, force)
//   PAKData                                    -> pak
//   PAKData.Read(bytes)                        -> pak::read(bytes)
//   PAKData.ToBytes()                          -> pak::to_bytes()
//   PAKData.Write(path)                        -> pak::write(path)
//   PAKData.ConvertImages(format)              -> pak::convert_images(format)
//   PAKData.Sprites / SpriteCount              -> pak::sprites / sprites.size() (pak::sprite_count is the count read)
//   PAKData.SpriteEntryLengthOffsets           -> pak::sprite_entries (offset, size)
//   Sprite                                     -> sprite
//   Sprite.Rectangles / Sprite.data            -> sprite::sprite_rectangles / sprite::image_data
//   Sprite.ColorKey get / set (null = none)    -> get_color_key() / set_color_key(value) (0 = none), has_color_key()
//   Sprite.ColorKeyR / G / B                   -> color_key_r() / color_key_g() / color_key_b() (0 when none)
//   Sprite.SetColorKey(r, g, b)                -> set_color_key(r, g, b)
//   Sprite.ClearColorKey()                     -> clear_color_key()
//   Sprite.ImageFormat                         -> sprite::image_format()
//   Sprite.ConvertToBmp() / ConvertToPng()     -> sprite::convert_to_bmp() / sprite::convert_to_png()
//   SpriteRectangle (x, y, width, height, pivotX, pivotY) -> sprite_rect (same field names)
//   SpriteRectangle.Size()                     -> sizeof(sprite_rect) (12)
//   Headers.FileHeader / SpriteHeader          -> file_header / sprite_header
//   FileHeader / SpriteHeader .Default().Magic -> file_header_signature / sprite_header_signature
//   SpriteImageFormat { Unknown, Bmp, Png }    -> sprite_image_format { unknown, bmp, png }
//   SpriteImage.DefaultColorKey / MaxDimension -> sprite_image::default_color_key / sprite_image::max_dimension
//   SpriteImage.GetFormat(data)                -> sprite_image::get_format(data)
//   SpriteImage.TryGetSize(data, out w, out h) -> sprite_image::try_get_size(data, w, h)
//   SpriteImage.CheckSize(data)                -> sprite_image::check_size(data)
//   SpriteImage.GetBmpBitsPerPixel(data)       -> sprite_image::get_bmp_bits_per_pixel(data)
//   SpriteImage.ToBmp24(data, colorKey)        -> sprite_image::to_bmp24(data, color_key) (0 = null)
//   SpriteImage.ToPng(data, colorKey)          -> sprite_image::to_png(data, color_key) (0 = null)
//   PAKRepair.Repair(bytes, force)             -> pak_repair::repair(bytes, force)
//   PAKRepair.Repair(bytes, start, end, force) -> pak_repair::repair(bytes, start, end, force)
//   PAKRepairReport (Data, Issues, PlaceholderSprites, Success) -> repair_report (data, issues, placeholder_sprites, success())
//   PAKRepairIssue (SpriteIndex, Offset, Description, Repaired, ToString())
//                                              -> repair_issue (sprite_index (-1 = null), offset, description, repaired, to_string())
//   BinaryReaderExtensions (PeekBytes, SkipBytes, EndOfStream on .NET's BinaryReader) -> no public counterpart
//                                                 (C++ has no BinaryReader; the readers use PAKLib::detail helpers)
//   Exceptions: InvalidDataException -> invalid_data_error; ArgumentException -> std::invalid_argument;
//               ArgumentOutOfRangeException -> argument_out_of_range_error (a std::invalid_argument);
//               FileNotFoundException -> file_not_found_error; NotSupportedException -> not_supported_error;
//               the plain Exception for a sprite position mismatch -> std::runtime_error;
//               zlib data errors keep .NET's unwrapped InvalidDataException text (see detail::inflate_error).
// Extras with no PAKLib counterpart:
//   get_sprite(path, index)       one sprite, with PAK.ReadFromFile's path checks and PAKData.Read's per-sprite checks
//   loadpak_fast(path), get_sprite_fast(path, index)   the game's lenient readers: no path, magic or image checks,
//                                 but the same count, bounds and index rules as loadpak / get_sprite
//   pak::header, pak::sprite_count, sprite::header   the file header, the count as read and each sprite's
//                                 100-byte header as read (PAKLib reads and discards them)
// All readers accept relative and absolute paths and open them exactly as given; no path is stored.
// Everything in PAKLib::detail is implementation detail, not API.
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <queue>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

// Language level: MSVC leaves __cplusplus at 199711L unless /Zc:__cplusplus is given, so prefer _MSVC_LANG
#if !defined(PAKLIB_CPLUSPLUS)
#if defined(_MSVC_LANG)
#define PAKLIB_CPLUSPLUS _MSVC_LANG
#else
#define PAKLIB_CPLUSPLUS __cplusplus
#endif
#endif

// C++17 resolves paths with std::filesystem; C++14 falls back to the C runtime (_mbstowcs_s_l and _wfullpath / getcwd)
#define PAKLIB_HAS_FILESYSTEM 0
#if PAKLIB_CPLUSPLUS >= 201703L && defined(__has_include)
#if __has_include(<filesystem>)
#undef PAKLIB_HAS_FILESYSTEM
#define PAKLIB_HAS_FILESYSTEM 1
#endif
#endif

#if PAKLIB_HAS_FILESYSTEM
#include <filesystem>
#else
#if defined(_MSC_VER)
// In MSVC's "file(line): warning CODE:" form, so MSBuild and the IDE list it as a warning (never an error)
#define PAKLIB_STRINGIZE_(x) #x
#define PAKLIB_STRINGIZE(x) PAKLIB_STRINGIZE_(x)
#pragma message(__FILE__ "(" PAKLIB_STRINGIZE(__LINE__) "): warning PAKLIB0001: PAKLib: compiling as C++14; repair_file path resolution falls back to C runtime calls. Upgrade to C++17 for portable std::filesystem support.")
#undef PAKLIB_STRINGIZE
#undef PAKLIB_STRINGIZE_
#else
#warning "PAKLib: compiling as C++14; repair_file path resolution falls back to C runtime calls. Upgrade to C++17 for portable std::filesystem support."
#endif
#if defined(_WIN32)
#include <locale.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif
#endif

namespace PAKLib {
	// ---------------------------------------------------------------------------------------------
	// Exceptions (mirroring the .NET exception types PAKLib throws)
	// ---------------------------------------------------------------------------------------------

	// InvalidDataException
	class invalid_data_error : public std::runtime_error {
	public:
		explicit invalid_data_error(const std::string& message) : std::runtime_error(message) {}
	};

	// FileNotFoundException; file_name() is FileNotFoundException.FileName
	class file_not_found_error : public std::runtime_error {
	public:
		file_not_found_error(const std::string& message, const std::string& file_name)
			: std::runtime_error(message), file_name_(file_name) {}
		const std::string& file_name() const { return file_name_; }
	private:
		std::string file_name_;
	};

	// NotSupportedException
	class not_supported_error : public std::runtime_error {
	public:
		explicit not_supported_error(const std::string& message) : std::runtime_error(message) {}
	};

	// ArgumentOutOfRangeException, which is an ArgumentException
	class argument_out_of_range_error : public std::invalid_argument {
	public:
		explicit argument_out_of_range_error(const std::string& message) : std::invalid_argument(message) {}
	};

	// ---------------------------------------------------------------------------------------------
	// On-disk records
	// ---------------------------------------------------------------------------------------------
#pragma pack(push, 1)
	struct sprite_rect {
		uint16_t x;
		uint16_t y;
		uint16_t width;
		uint16_t height;
		int16_t pivotX;
		int16_t pivotY;
	};

	constexpr size_t file_header_size = 17;
	constexpr char file_header_signature[file_header_size + 1] = "<Pak file header>";
	struct file_header {
		char signature[file_header_size];
		uint8_t padding[3]{ 0 };
	};

	constexpr size_t sprite_header_size = 20;
	constexpr char sprite_header_signature[sprite_header_size + 1] = "<Sprite File Header>";
	struct sprite_header {
		char signature[sprite_header_size];
		uint8_t padding[80]{ 0 };
	};

	struct sprite_entry {
		uint32_t offset{ 0 };
		uint32_t size{ 0 };
	};
#pragma pack(pop)

	enum class sprite_image_format {
		unknown,
		bmp,
		png
	};

	struct sprite {
	public:
		sprite_header header{};
		std::vector<sprite_rect> sprite_rectangles{};
		std::vector<uint8_t> image_data; // Raw image data (BMP or PNG)

		// Authored transparency color key as 0xFFRRGGBB (stored on disk as bytes A RR GG BB).
		// 0 means none was authored, in which case the key is derived from the first pixel of the sprite sheet.
		// Alpha only marks the key as set: any value whose alpha is not 0xFF (including legacy 00 RR GG BB)
		// is treated as unset, so black (FF 00 00 00) stays distinct from none (00 00 00 00).
		uint32_t get_color_key() const { return color_key; }
		void set_color_key(uint32_t value) { color_key = (value >> 24) == 0xFF ? value : 0; }
		void set_color_key(uint8_t r, uint8_t g, uint8_t b) {
			set_color_key(0xFF000000u | (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b);
		}
		void clear_color_key() { color_key = 0; }
		bool has_color_key() const { return color_key != 0; }

		uint8_t color_key_r() const { return static_cast<uint8_t>(color_key >> 16); }
		uint8_t color_key_g() const { return static_cast<uint8_t>(color_key >> 8); }
		uint8_t color_key_b() const { return static_cast<uint8_t>(color_key); }

		// Sprite.ImageFormat
		sprite_image_format image_format() const;

		// Converts the image to a 24bpp BMP. Transparent PNG pixels are filled with the color key; a PNG without
		// a color key gets sprite_image::default_color_key (magenta) as its key.
		void convert_to_bmp();

		// Converts the image to an 8-bit RGBA PNG. BMP pixels matching the color key (or the top-left pixel when
		// there is none) become transparent. A BMP without a color key gets the top-left color as its key, so
		// converting back to BMP restores those pixels.
		void convert_to_png();

	private:
		uint32_t color_key{ 0 };
	};

	struct pak {
	public:
		file_header header{};
		uint32_t sprite_count{ 0 };            // the count read from the file
		std::vector<sprite_entry> sprite_entries;
		std::vector<sprite> sprites{};

		// PAKData.Read: parses a whole PAK held in memory
		static pak read(const std::vector<uint8_t>& bytes);

		// PAKData.ToBytes: sequential layout, zeroed padding, colour key as 00000000 or FF RR GG BB.
		// BMPs that are not 24bpp are written as 24bpp (the sprites are not changed).
		std::vector<uint8_t> to_bytes() const;

		// PAKData.Write
		void write(const std::string& file_path) const;

		// PAKData.ConvertImages: every sprite to a 24bpp BMP or an 8-bit RGBA PNG
		void convert_images(sprite_image_format format);
	};

	// PAKRepairIssue
	struct repair_issue {
		// Position of the sprite in the original file order (or the sprite table index for table issues),
		// or -1 for file-level issues (C# null)
		int sprite_index = -1;
		// Byte offset in the input where the issue was found
		int64_t offset = 0;
		std::string description;
		bool repaired = false;

		std::string to_string() const {
			const std::string where = sprite_index >= 0 ? "sprite " + std::to_string(sprite_index) : std::string("file");
			return "[" + std::string(repaired ? "repaired" : "not repaired") + "] " + where + " @ " +
				std::to_string(static_cast<long long>(offset)) + ": " + description;
		}
	};

	// PAKRepairReport
	struct repair_report {
		// The salvaged sprites, ready to be written with pak::write or save_to_file
		pak data;
		std::vector<repair_issue> issues;
		// Indices in data.sprites of the 32x32 placeholders inserted for corrupt sprites (force only)
		std::vector<int> placeholder_sprites;
		// True when every issue found was repaired (nothing was dropped)
		bool success() const {
			for (const repair_issue& issue : issues)
				if (!issue.repaired)
					return false;
			return true;
		}
	};

	// ---------------------------------------------------------------------------------------------
	// Implementation detail: byte access, checksums, inflate / deflate
	// ---------------------------------------------------------------------------------------------
	namespace detail {
		using bytes = std::vector<uint8_t>;

		// The signatures as class template statics, for the inline functions that take their address
		// (file_header_signature / sprite_header_signature are internal-linkage copies per translation unit)
		template <typename T = void>
		struct magic_t {
			static const char file[file_header_size + 1];
			static const char sprite[sprite_header_size + 1];
		};
		template <typename T> const char magic_t<T>::file[file_header_size + 1] = "<Pak file header>";
		template <typename T> const char magic_t<T>::sprite[sprite_header_size + 1] = "<Sprite File Header>";
		typedef magic_t<> magic;

		inline int32_t to_i32(uint32_t u) {
			return u <= 0x7FFFFFFFu ? static_cast<int32_t>(u) : -static_cast<int32_t>(~u) - 1;
		}
		inline int16_t to_i16(uint16_t u) {
			return u <= 0x7FFFu ? static_cast<int16_t>(u) : static_cast<int16_t>(-static_cast<int>(static_cast<uint16_t>(~u)) - 1);
		}
		inline int64_t wrap_i64(uint64_t u) {
			return u <= 0x7FFFFFFFFFFFFFFFull ? static_cast<int64_t>(u) : -static_cast<int64_t>(~u) - 1;
		}
		inline uint16_t u16le(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
		inline int16_t i16le(const uint8_t* p) { return to_i16(u16le(p)); }
		inline uint32_t u32le(const uint8_t* p) {
			return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
				(static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
		}
		inline int32_t i32le(const uint8_t* p) { return to_i32(u32le(p)); }
		inline uint32_t u32be(const uint8_t* p) {
			return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
				(static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
		}
		inline int32_t i32be(const uint8_t* p) { return to_i32(u32be(p)); }

		inline void put_u16le(bytes& out, uint16_t v) {
			out.push_back(static_cast<uint8_t>(v));
			out.push_back(static_cast<uint8_t>(v >> 8));
		}
		inline void put_u32le(bytes& out, uint32_t v) {
			for (int i = 0; i < 4; ++i)
				out.push_back(static_cast<uint8_t>(v >> (8 * i)));
		}
		inline void put_u32be(bytes& out, uint32_t v) {
			for (int i = 3; i >= 0; --i)
				out.push_back(static_cast<uint8_t>(v >> (8 * i)));
		}
		inline void set_u16le(bytes& out, size_t at, uint16_t v) {
			out[at] = static_cast<uint8_t>(v);
			out[at + 1] = static_cast<uint8_t>(v >> 8);
		}
		inline void set_u32le(bytes& out, size_t at, uint32_t v) {
			for (size_t i = 0; i < 4; ++i)
				out[at + i] = static_cast<uint8_t>(v >> (8 * i));
		}

		inline std::string num(long long v) { return std::to_string(v); }

		// Encoding.ASCII.GetString: bytes above 0x7F become '?'. NUL also becomes '?' here, because an exception
		// message (what()) ends at the first NUL; .NET keeps it.
		inline std::string ascii(const uint8_t* p, size_t n) {
			std::string s;
			for (size_t i = 0; i < n; ++i)
				s.push_back(p[i] > 0x7F || p[i] == 0 ? '?' : static_cast<char>(p[i]));
			return s;
		}

		// ---- Strings: .NET string semantics on code points ----
		// One code point decoded from UTF-8 at i (advancing i). Invalid or overlong sequences, surrogates and values
		// above U+10FFFF yield 0x110000 + the byte, so a stray byte only ever matches the same stray byte.
		inline uint32_t next_code_point(const std::string& s, size_t& i) {
			const uint8_t b0 = static_cast<uint8_t>(s[i]);
			const size_t left = s.size() - i;
			size_t n = 0;
			uint32_t cp = 0, min = 0;
			if (b0 < 0x80) { ++i; return b0; }
			else if ((b0 & 0xE0) == 0xC0) { n = 2; cp = b0 & 0x1Fu; min = 0x80; }
			else if ((b0 & 0xF0) == 0xE0) { n = 3; cp = b0 & 0x0Fu; min = 0x800; }
			else if ((b0 & 0xF8) == 0xF0) { n = 4; cp = b0 & 0x07u; min = 0x10000; }
			bool ok = n != 0 && left >= n;
			for (size_t k = 1; ok && k < n; ++k) {
				const uint8_t b = static_cast<uint8_t>(s[i + k]);
				ok = (b & 0xC0) == 0x80;
				cp = (cp << 6) | (b & 0x3Fu);
			}
			if (!ok || cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
				++i;
				return 0x110000u + b0;
			}
			i += n;
			return cp;
		}

		// char.IsWhiteSpace: U+0009-000D, 0020, 0085, 00A0 and the Unicode space, line and paragraph separators
		inline bool is_white_space(uint32_t cp) {
			return (cp >= 0x09 && cp <= 0x0D) || cp == 0x20 || cp == 0x85 || cp == 0xA0 || cp == 0x1680
				|| (cp >= 0x2000 && cp <= 0x200A) || cp == 0x2028 || cp == 0x2029 || cp == 0x202F || cp == 0x205F || cp == 0x3000;
		}

		inline char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

		// OrdinalIgnoreCase's upper-casing (System.Globalization.OrdinalCasing, ICU mode): the simple upper-case
		// mapping per character, supplementary characters folded as whole surrogate pairs, and U+0131 / U+017F
		// deliberately not folded to I / S. Taken from .NET 8.0 (Unicode 15.0) by reflection over every code point
		// and cross-checked against Python's unicodedata 15.1 (which differs only where .NET 8 predates it and in the
		// two exclusions). Each range maps lo, lo + step, ..., hi to cp + delta. 198 ranges.
		struct casing_range { uint32_t lo, hi; int32_t delta; uint32_t step; };

		inline uint32_t ordinal_upper(uint32_t cp) {
			static const casing_range ranges[] = {
				{ 0x0061, 0x007A, -32, 1 }, { 0x00B5, 0x00B5, 743, 1 }, { 0x00E0, 0x00F6, -32, 1 }, { 0x00F8, 0x00FE, -32, 1 },
				{ 0x00FF, 0x00FF, 121, 1 }, { 0x0101, 0x012F, -1, 2 }, { 0x0133, 0x0137, -1, 2 }, { 0x013A, 0x0148, -1, 2 },
				{ 0x014B, 0x0177, -1, 2 }, { 0x017A, 0x017E, -1, 2 }, { 0x0180, 0x0180, 195, 1 }, { 0x0183, 0x0185, -1, 2 },
				{ 0x0188, 0x0188, -1, 1 }, { 0x018C, 0x018C, -1, 1 }, { 0x0192, 0x0192, -1, 1 }, { 0x0195, 0x0195, 97, 1 },
				{ 0x0199, 0x0199, -1, 1 }, { 0x019A, 0x019A, 163, 1 }, { 0x019E, 0x019E, 130, 1 }, { 0x01A1, 0x01A5, -1, 2 },
				{ 0x01A8, 0x01A8, -1, 1 }, { 0x01AD, 0x01AD, -1, 1 }, { 0x01B0, 0x01B0, -1, 1 }, { 0x01B4, 0x01B6, -1, 2 },
				{ 0x01B9, 0x01B9, -1, 1 }, { 0x01BD, 0x01BD, -1, 1 }, { 0x01BF, 0x01BF, 56, 1 }, { 0x01C5, 0x01C5, -1, 1 },
				{ 0x01C6, 0x01C6, -2, 1 }, { 0x01C8, 0x01C8, -1, 1 }, { 0x01C9, 0x01C9, -2, 1 }, { 0x01CB, 0x01CB, -1, 1 },
				{ 0x01CC, 0x01CC, -2, 1 }, { 0x01CE, 0x01DC, -1, 2 }, { 0x01DD, 0x01DD, -79, 1 }, { 0x01DF, 0x01EF, -1, 2 },
				{ 0x01F2, 0x01F2, -1, 1 }, { 0x01F3, 0x01F3, -2, 1 }, { 0x01F5, 0x01F5, -1, 1 }, { 0x01F9, 0x021F, -1, 2 },
				{ 0x0223, 0x0233, -1, 2 }, { 0x023C, 0x023C, -1, 1 }, { 0x023F, 0x0240, 10815, 1 }, { 0x0242, 0x0242, -1, 1 },
				{ 0x0247, 0x024F, -1, 2 }, { 0x0250, 0x0250, 10783, 1 }, { 0x0251, 0x0251, 10780, 1 }, { 0x0252, 0x0252, 10782, 1 },
				{ 0x0253, 0x0253, -210, 1 }, { 0x0254, 0x0254, -206, 1 }, { 0x0256, 0x0257, -205, 1 }, { 0x0259, 0x0259, -202, 1 },
				{ 0x025B, 0x025B, -203, 1 }, { 0x025C, 0x025C, 42319, 1 }, { 0x0260, 0x0260, -205, 1 }, { 0x0261, 0x0261, 42315, 1 },
				{ 0x0263, 0x0263, -207, 1 }, { 0x0265, 0x0265, 42280, 1 }, { 0x0266, 0x0266, 42308, 1 }, { 0x0268, 0x0268, -209, 1 },
				{ 0x0269, 0x0269, -211, 1 }, { 0x026A, 0x026A, 42308, 1 }, { 0x026B, 0x026B, 10743, 1 }, { 0x026C, 0x026C, 42305, 1 },
				{ 0x026F, 0x026F, -211, 1 }, { 0x0271, 0x0271, 10749, 1 }, { 0x0272, 0x0272, -213, 1 }, { 0x0275, 0x0275, -214, 1 },
				{ 0x027D, 0x027D, 10727, 1 }, { 0x0280, 0x0280, -218, 1 }, { 0x0282, 0x0282, 42307, 1 }, { 0x0283, 0x0283, -218, 1 },
				{ 0x0287, 0x0287, 42282, 1 }, { 0x0288, 0x0288, -218, 1 }, { 0x0289, 0x0289, -69, 1 }, { 0x028A, 0x028B, -217, 1 },
				{ 0x028C, 0x028C, -71, 1 }, { 0x0292, 0x0292, -219, 1 }, { 0x029D, 0x029D, 42261, 1 }, { 0x029E, 0x029E, 42258, 1 },
				{ 0x0345, 0x0345, 84, 1 }, { 0x0371, 0x0373, -1, 2 }, { 0x0377, 0x0377, -1, 1 }, { 0x037B, 0x037D, 130, 1 },
				{ 0x03AC, 0x03AC, -38, 1 }, { 0x03AD, 0x03AF, -37, 1 }, { 0x03B1, 0x03C1, -32, 1 }, { 0x03C2, 0x03C2, -31, 1 },
				{ 0x03C3, 0x03CB, -32, 1 }, { 0x03CC, 0x03CC, -64, 1 }, { 0x03CD, 0x03CE, -63, 1 }, { 0x03D0, 0x03D0, -62, 1 },
				{ 0x03D1, 0x03D1, -57, 1 }, { 0x03D5, 0x03D5, -47, 1 }, { 0x03D6, 0x03D6, -54, 1 }, { 0x03D7, 0x03D7, -8, 1 },
				{ 0x03D9, 0x03EF, -1, 2 }, { 0x03F0, 0x03F0, -86, 1 }, { 0x03F1, 0x03F1, -80, 1 }, { 0x03F2, 0x03F2, 7, 1 },
				{ 0x03F3, 0x03F3, -116, 1 }, { 0x03F5, 0x03F5, -96, 1 }, { 0x03F8, 0x03F8, -1, 1 }, { 0x03FB, 0x03FB, -1, 1 },
				{ 0x0430, 0x044F, -32, 1 }, { 0x0450, 0x045F, -80, 1 }, { 0x0461, 0x0481, -1, 2 }, { 0x048B, 0x04BF, -1, 2 },
				{ 0x04C2, 0x04CE, -1, 2 }, { 0x04CF, 0x04CF, -15, 1 }, { 0x04D1, 0x052F, -1, 2 }, { 0x0561, 0x0586, -48, 1 },
				{ 0x10D0, 0x10FA, 3008, 1 }, { 0x10FD, 0x10FF, 3008, 1 }, { 0x13F8, 0x13FD, -8, 1 }, { 0x1C80, 0x1C80, -6254, 1 },
				{ 0x1C81, 0x1C81, -6253, 1 }, { 0x1C82, 0x1C82, -6244, 1 }, { 0x1C83, 0x1C84, -6242, 1 }, { 0x1C85, 0x1C85, -6243, 1 },
				{ 0x1C86, 0x1C86, -6236, 1 }, { 0x1C87, 0x1C87, -6181, 1 }, { 0x1C88, 0x1C88, 35266, 1 }, { 0x1D79, 0x1D79, 35332, 1 },
				{ 0x1D7D, 0x1D7D, 3814, 1 }, { 0x1D8E, 0x1D8E, 35384, 1 }, { 0x1E01, 0x1E95, -1, 2 }, { 0x1E9B, 0x1E9B, -59, 1 },
				{ 0x1EA1, 0x1EFF, -1, 2 }, { 0x1F00, 0x1F07, 8, 1 }, { 0x1F10, 0x1F15, 8, 1 }, { 0x1F20, 0x1F27, 8, 1 },
				{ 0x1F30, 0x1F37, 8, 1 }, { 0x1F40, 0x1F45, 8, 1 }, { 0x1F51, 0x1F57, 8, 2 }, { 0x1F60, 0x1F67, 8, 1 },
				{ 0x1F70, 0x1F71, 74, 1 }, { 0x1F72, 0x1F75, 86, 1 }, { 0x1F76, 0x1F77, 100, 1 }, { 0x1F78, 0x1F79, 128, 1 },
				{ 0x1F7A, 0x1F7B, 112, 1 }, { 0x1F7C, 0x1F7D, 126, 1 }, { 0x1F80, 0x1F87, 8, 1 }, { 0x1F90, 0x1F97, 8, 1 },
				{ 0x1FA0, 0x1FA7, 8, 1 }, { 0x1FB0, 0x1FB1, 8, 1 }, { 0x1FB3, 0x1FB3, 9, 1 }, { 0x1FBE, 0x1FBE, -7205, 1 },
				{ 0x1FC3, 0x1FC3, 9, 1 }, { 0x1FD0, 0x1FD1, 8, 1 }, { 0x1FE0, 0x1FE1, 8, 1 }, { 0x1FE5, 0x1FE5, 7, 1 },
				{ 0x1FF3, 0x1FF3, 9, 1 }, { 0x214E, 0x214E, -28, 1 }, { 0x2170, 0x217F, -16, 1 }, { 0x2184, 0x2184, -1, 1 },
				{ 0x24D0, 0x24E9, -26, 1 }, { 0x2C30, 0x2C5F, -48, 1 }, { 0x2C61, 0x2C61, -1, 1 }, { 0x2C65, 0x2C65, -10795, 1 },
				{ 0x2C66, 0x2C66, -10792, 1 }, { 0x2C68, 0x2C6C, -1, 2 }, { 0x2C73, 0x2C73, -1, 1 }, { 0x2C76, 0x2C76, -1, 1 },
				{ 0x2C81, 0x2CE3, -1, 2 }, { 0x2CEC, 0x2CEE, -1, 2 }, { 0x2CF3, 0x2CF3, -1, 1 }, { 0x2D00, 0x2D25, -7264, 1 },
				{ 0x2D27, 0x2D27, -7264, 1 }, { 0x2D2D, 0x2D2D, -7264, 1 }, { 0xA641, 0xA66D, -1, 2 }, { 0xA681, 0xA69B, -1, 2 },
				{ 0xA723, 0xA72F, -1, 2 }, { 0xA733, 0xA76F, -1, 2 }, { 0xA77A, 0xA77C, -1, 2 }, { 0xA77F, 0xA787, -1, 2 },
				{ 0xA78C, 0xA78C, -1, 1 }, { 0xA791, 0xA793, -1, 2 }, { 0xA794, 0xA794, 48, 1 }, { 0xA797, 0xA7A9, -1, 2 },
				{ 0xA7B5, 0xA7C3, -1, 2 }, { 0xA7C8, 0xA7CA, -1, 2 }, { 0xA7D1, 0xA7D1, -1, 1 }, { 0xA7D7, 0xA7D9, -1, 2 },
				{ 0xA7F6, 0xA7F6, -1, 1 }, { 0xAB53, 0xAB53, -928, 1 }, { 0xAB70, 0xABBF, -38864, 1 }, { 0xFF41, 0xFF5A, -32, 1 },
				{ 0x10428, 0x1044F, -40, 1 }, { 0x104D8, 0x104FB, -40, 1 }, { 0x10597, 0x105A1, -39, 1 }, { 0x105A3, 0x105B1, -39, 1 },
				{ 0x105B3, 0x105B9, -39, 1 }, { 0x105BB, 0x105BC, -39, 1 }, { 0x10CC0, 0x10CF2, -64, 1 }, { 0x118C0, 0x118DF, -32, 1 },
				{ 0x16E60, 0x16E7F, -32, 1 }, { 0x1E922, 0x1E943, -34, 1 },
			};
			if (cp < 0x80)
				return cp >= 'a' && cp <= 'z' ? cp - 32 : cp;
			size_t lo = 0, hi = sizeof(ranges) / sizeof(ranges[0]);
			while (lo < hi) {
				const size_t mid = (lo + hi) / 2;
				if (ranges[mid].hi < cp)
					lo = mid + 1;
				else
					hi = mid;
			}
			if (lo < sizeof(ranges) / sizeof(ranges[0]) && ranges[lo].lo <= cp && (cp - ranges[lo].lo) % ranges[lo].step == 0)
				return static_cast<uint32_t>(static_cast<int32_t>(cp) + ranges[lo].delta);
			return cp;
		}

		// UTF-8 to code points, a stray byte as 0x110000 + the byte (see next_code_point)
		inline std::u32string utf8_code_points(const std::string& s) {
			std::u32string out;
			for (size_t i = 0; i < s.size();)
				out.push_back(static_cast<char32_t>(next_code_point(s, i)));
			return out;
		}

		// string.Equals(a, b, StringComparison.OrdinalIgnoreCase) on code points
		inline bool equals_ignore_case(const std::u32string& a, const std::u32string& b) {
			if (a.size() != b.size())
				return false;
			for (size_t i = 0; i < a.size(); ++i)
				if (ordinal_upper(a[i]) != ordinal_upper(b[i]))
					return false;
			return true;
		}

		// The same on UTF-8 text
		inline bool equals_ignore_case(const std::string& a, const std::string& b) {
			return equals_ignore_case(utf8_code_points(a), utf8_code_points(b));
		}

		// ---- Paths: decoded exactly as the file functions decode them ----
		// Paths are narrow strings, opened with std::ifstream. On Windows the C runtime (like CreateFileA) decodes
		// them with the ANSI code page; elsewhere they are bytes, taken as UTF-8. .NET judges the same names as UTF-16,
		// so every check on a path (blank, .epak, same file) works on the code points of that same decoding: two
		// spellings the file system opens as one file are one path here too.

		// UTF-16 units to code points: surrogate pairs combined (OrdinalIgnoreCase folds them whole), lone ones kept
		inline std::u32string utf16_code_points(const std::wstring& w) {
			std::u32string out;
			for (size_t i = 0; i < w.size(); ++i) {
				const uint32_t unit = static_cast<uint32_t>(w[i]);
				if (unit >= 0xD800 && unit <= 0xDBFF && i + 1 < w.size()) {
					const uint32_t low = static_cast<uint32_t>(w[i + 1]);
					if (low >= 0xDC00 && low <= 0xDFFF) {
						out.push_back(static_cast<char32_t>(0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00)));
						++i;
						continue;
					}
				}
				out.push_back(static_cast<char32_t>(unit));
			}
			return out;
		}

#if defined(_WIN32)
		// The narrow path as Windows' file functions decode it (the ANSI code page); false when it does not decode
		inline bool native_wide(const std::string& path, std::wstring& out) {
#if PAKLIB_HAS_FILESYSTEM
			try {
				out = std::filesystem::path(path).wstring();   // the code page the file functions use
				return true;
			}
			catch (const std::exception&) {
				return false;
			}
#else
			static const _locale_t acp = _create_locale(LC_ALL, ".ACP");
			if (acp == nullptr)
				return false;
			std::vector<wchar_t> buffer(path.size() + 1);
			size_t converted = 0;
			if (_mbstowcs_s_l(&converted, buffer.data(), buffer.size(), path.c_str(), _TRUNCATE, acp) != 0)
				return false;
			out.assign(buffer.data());
			return true;
#endif
		}
#endif

		// The path's characters as the file functions see them (UTF-8 bytes when they cannot be decoded)
		inline std::u32string path_code_points(const std::string& path) {
#if defined(_WIN32)
			std::wstring wide;
			if (native_wide(path, wide))
				return utf16_code_points(wide);
#endif
			return utf8_code_points(path);
		}

		// string.IsNullOrWhiteSpace on a path (an empty string is the C++ "null or empty")
		inline bool is_null_or_white_space(const std::string& path) {
			for (char32_t c : path_code_points(path))
				if (!is_white_space(c))
					return false;
			return true;
		}

		// Path.GetExtension(Path.GetFileName(path)).ToLower() (en-US / invariant), compared with ".epak": ASCII is
		// lowered, plus the two characters whose lower case is ASCII (U+0130 -> i, U+212A KELVIN SIGN -> k)
		inline std::u32string lower_extension(const std::u32string& cps) {
			const size_t separator = cps.find_last_of(U"/\\:");
			const std::u32string name = separator == std::u32string::npos ? cps : cps.substr(separator + 1);
			const size_t dot = name.find_last_of(U'.');
			if (dot == std::u32string::npos || dot + 1 == name.size())
				return std::u32string();
			std::u32string ext;
			for (size_t i = dot; i < name.size(); ++i) {
				const char32_t c = name[i];
				ext.push_back(c == 0x130 ? U'i' : c == 0x212A ? U'k' : (c >= U'A' && c <= U'Z') ? static_cast<char32_t>(c + 32) : c);
			}
			return ext;
		}

		inline std::u32string lower_extension(const std::string& path) {
			return lower_extension(path_code_points(path));
		}

		// Path.GetFullPath: the one place a path is resolved (repair_file's "output must differ from input" check),
		// as code points in the file functions' decoding. Lexical, against the working directory: nothing has to
		// exist and links are not followed, as in .NET.
		//   C++17: std::filesystem::absolute (GetFullPathNameW on Windows, the call .NET makes) then lexically_normal
		//          (drops "." and ".." on POSIX, where absolute only prepends the working directory). weakly_canonical
		//          is not used: it resolves links and the on-disk case of the existing prefix, which .NET does not.
		//   C++14: _wfullpath on Windows, getcwd plus the same lexical clean-up elsewhere.
		inline std::u32string full_path(const std::string& path) {
#if PAKLIB_HAS_FILESYSTEM
			namespace fs = std::filesystem;
			try {
				std::error_code error;
				const fs::path absolute = fs::absolute(fs::path(path), error);
				if (!error) {
#if defined(_WIN32)
					return utf16_code_points(absolute.lexically_normal().wstring());
#else
					return utf8_code_points(absolute.lexically_normal().string());
#endif
				}
			}
			catch (const std::exception&) {
			}
			return path_code_points(path);   // not resolvable: compared as given
#elif defined(_WIN32)
			std::wstring wide;
			if (!native_wide(path, wide))
				return utf8_code_points(path);
			std::vector<wchar_t> buffer(32768);
			if (_wfullpath(buffer.data(), wide.c_str(), buffer.size()) != nullptr)
				return utf16_code_points(std::wstring(buffer.data()));
			return utf16_code_points(wide);
#else
			std::string joined = path;
			if (path.empty() || path[0] != '/') {
				char buffer[4096];
				if (getcwd(buffer, sizeof(buffer)) == nullptr)
					return utf8_code_points(path);
				joined = std::string(buffer) + "/" + path;
			}
			std::vector<std::string> parts;
			size_t at = 0;
			while (at <= joined.size()) {
				size_t next = joined.find('/', at);
				if (next == std::string::npos)
					next = joined.size();
				const std::string part = joined.substr(at, next - at);
				if (part == "..") {
					if (!parts.empty())
						parts.pop_back();
				}
				else if (!part.empty() && part != ".") {
					parts.push_back(part);
				}
				at = next + 1;
			}
			std::string result;
			for (const std::string& part : parts)
				result += "/" + part;
			return utf8_code_points(result.empty() ? "/" : result);
#endif
		}

		// Path.GetFullPath(a) and Path.GetFullPath(b) equal under StringComparison.OrdinalIgnoreCase
		inline bool same_full_path(const std::string& a, const std::string& b) {
			return equals_ignore_case(full_path(a), full_path(b));
		}

		// File.Exists: true for an existing file, false for a directory or a missing path. The path is interpreted
		// as std::ifstream interprets it (the native narrow encoding), so the check matches what the readers open.
		inline bool file_exists(const std::string& path) {
#if PAKLIB_HAS_FILESYSTEM
			namespace fs = std::filesystem;
			try {
				std::error_code error;
				const fs::file_status status = fs::status(fs::path(path), error);
				if (error || !fs::exists(status))
					return false;
				return !fs::is_directory(status);
			}
			catch (const std::exception&) {
				std::ifstream file(path, std::ios::binary);   // a name the narrow conversion refuses
				return file.is_open();
			}
#else
#if !defined(_WIN32)
			struct stat info;
			if (stat(path.c_str(), &info) != 0 || S_ISDIR(info.st_mode))
				return false;
#endif
			std::ifstream file(path, std::ios::binary);   // on Windows a directory does not open
			return file.is_open();
#endif
		}

		inline bytes read_all_bytes(const std::string& path) {
			std::ifstream file(path, std::ios::binary);
			if (!file.is_open())
				throw std::runtime_error("Failed to open file: " + path);
			bytes data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
			if (file.bad())
				throw std::runtime_error("Failed to read file: " + path);
			return data;
		}

		inline void write_all_bytes(const std::string& path, const bytes& data) {
			std::ofstream file(path, std::ios::binary | std::ios::trunc);
			if (!file.is_open())
				throw std::runtime_error("Failed to create file: " + path);
			file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
			file.flush();
			if (!file)
				throw std::runtime_error("Failed to write file: " + path);
		}

		// ---- CRC-32 (PNG chunks) and Adler-32 (zlib) ----
		struct crc_table_holder {
			uint32_t table[256];
			crc_table_holder() {
				for (uint32_t n = 0; n < 256; ++n) {
					uint32_t c = n;
					for (int k = 0; k < 8; ++k)
						c = (c & 1) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
					table[n] = c;
				}
			}
		};
		inline uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t length) {
			static const crc_table_holder holder;
			for (size_t i = 0; i < length; ++i)
				crc = holder.table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
			return crc;
		}

		inline uint32_t adler32(const uint8_t* data, size_t length) {
			uint32_t a = 1, b = 0;
			size_t i = 0;
			while (i < length) {
				const size_t block = std::min<size_t>(length - i, 5552);
				for (size_t j = 0; j < block; ++j) {
					a += data[i + j];
					b += a;
				}
				a %= 65521;
				b %= 65521;
				i += block;
			}
			return (b << 16) | a;
		}

		// ---- Inflate (RFC 1950 / 1951), bounded to `limit` output bytes, behaving as .NET 8's zlib does under the
		// ZLibStream read loop in PAKLib (measured against .NET 8.0 with crafted streams; see Parity\README.md):
		//   - running out of input is not an error (the caller sees fewer bytes);
		//   - once the output is full, zlib keeps decoding ahead into its window through the input .NET has already
		//     handed it (the current 8 KiB chunk), for up to 96 KiB of further output; code, table and distance
		//     errors met on the way are raised, and at that 96 KiB the next literal or match stops it (as zlib's
		//     slow path does when out of room), after its codes are read;
		//   - the Adler-32 trailer is checked only when the stream ended without output past `limit` (with output
		//     decoded ahead, the check waits for data PAKLib never reads).
		// .NET 9+ (zlib-ng) stops at the limit instead; PAKLib's reference is .NET 8. The extra output is dropped.
		// Malformed data throws invalid_data_error.
		struct inflate_input_end {};

		// .NET's Inflater turns every zlib Z_DATA_ERROR into InvalidDataException(SR.UnsupportedCompression), which
		// PAKLib's catch (IOException) lets through unchanged; the zlib reason is kept at each call site as a note only
		inline invalid_data_error inflate_error(const char* /* zlib reason */) {
			return invalid_data_error("The archive entry was compressed using an unsupported compression method.");
		}

		// Z_NEED_DICT (FDICT set) is a ZLibException (an IOException) with SR.ZLibErrorUnexpected, which PAKLib wraps
		inline invalid_data_error inflate_need_dictionary() {
			return invalid_data_error("PNG image data cannot be decompressed. The underlying compression routine returned an unexpected error code.");
		}

		// DeflateStream's input buffer size: .NET feeds zlib the compressed data in chunks of this many bytes
		constexpr size_t inflate_input_chunk = 8192;
		// How much output .NET 8's zlib decodes ahead past the requested size (measured: 98299 < x <= 98361)
		constexpr size_t inflate_decode_ahead = 98304;

		struct inflate_state {
			const uint8_t* in;
			size_t length;
			size_t pos;
			uint64_t buf;
			int cnt;
			bytes* out;
			size_t limit;     // the output PAKLib asks for
			size_t room;      // limit plus the decode-ahead: literals and matches stop here
			bool clamped;

			// Called after output is written: when the output is full, only the rest of the input chunk zlib
			// already holds stays available
			void on_output() {
				if (clamped || out->size() < limit)
					return;
				clamped = true;
				const size_t consumed = pos - static_cast<size_t>(cnt / 8);
				const size_t end = std::min(length, (consumed + inflate_input_chunk - 1) / inflate_input_chunk * inflate_input_chunk);
				if (pos > end) {
					cnt -= static_cast<int>((pos - end) * 8);
					buf &= cnt >= 64 ? ~static_cast<uint64_t>(0) : (static_cast<uint64_t>(1) << cnt) - 1;
					pos = end;
				}
				length = end;
			}

			void fill() {
				while (cnt <= 56 && pos < length) {
					buf |= static_cast<uint64_t>(in[pos++]) << cnt;
					cnt += 8;
				}
			}
			uint32_t bits(int n) {
				if (cnt < n) {
					fill();
					if (cnt < n)
						throw inflate_input_end{};
				}
				const uint32_t v = static_cast<uint32_t>(buf & ((static_cast<uint64_t>(1) << n) - 1));
				buf >>= n;
				cnt -= n;
				return v;
			}
		};

		struct huffman_table {
			uint16_t count[16];
			uint16_t symbol[320];
			uint16_t fast[1024];   // (length << 9) | symbol for codes up to 10 bits, 0 = use the slow path
		};

		inline uint32_t reverse_bits(uint32_t code, int length) {
			uint32_t r = 0;
			for (int i = 0; i < length; ++i) {
				r = (r << 1) | (code & 1);
				code >>= 1;
			}
			return r;
		}

		// Returns 0 for a complete code, > 0 for an incomplete one, < 0 for an over-subscribed one
		inline int build_huffman(huffman_table& h, const uint8_t* lengths, int n) {
			std::memset(h.count, 0, sizeof(h.count));
			std::memset(h.fast, 0, sizeof(h.fast));
			for (int s = 0; s < n; ++s)
				h.count[lengths[s]]++;
			if (h.count[0] == n)
				return 0;
			int left = 1;
			for (int len = 1; len <= 15; ++len) {
				left <<= 1;
				left -= h.count[len];
				if (left < 0)
					return left;
			}
			uint16_t offs[16];
			offs[1] = 0;
			for (int len = 1; len < 15; ++len)
				offs[len + 1] = static_cast<uint16_t>(offs[len] + h.count[len]);
			for (int s = 0; s < n; ++s)
				if (lengths[s] != 0)
					h.symbol[offs[lengths[s]]++] = static_cast<uint16_t>(s);
			uint32_t code = 0;
			int index = 0;
			for (int len = 1; len <= 15; ++len) {
				for (int c = 0; c < h.count[len]; ++c) {
					const uint16_t sym = h.symbol[index++];
					if (len <= 10) {
						const uint16_t entry = static_cast<uint16_t>((len << 9) | sym);
						for (uint32_t r = reverse_bits(code, len); r < 1024; r += 1u << len)
							h.fast[r] = entry;
					}
					++code;
				}
				code <<= 1;
			}
			return left;
		}

		inline int decode_symbol(inflate_state& s, const huffman_table& h) {
			if (s.cnt < 10)
				s.fill();
			if (s.cnt > 0) {
				const uint16_t entry = h.fast[s.buf & 1023];
				if (entry != 0) {
					const int len = entry >> 9;
					if (len <= s.cnt) {
						s.buf >>= len;
						s.cnt -= len;
						return entry & 511;
					}
				}
			}
			int code = 0, first = 0, index = 0;
			for (int len = 1; len <= 15; ++len) {
				code |= static_cast<int>(s.bits(1));
				const int count = h.count[len];
				if (code - count < first)
					return h.symbol[index + (code - first)];
				index += count;
				first += count;
				first <<= 1;
				code <<= 1;
			}
			throw inflate_error("Invalid Huffman code.");
		}

		// Deflate tables as static members of a class template: one definition across translation units, so
		// the inline functions using them stay ODR-safe in C++14 (no inline variables before C++17)
		template <typename T = void>
		struct deflate_tables_t {
			static const uint16_t length_base[29];
			static const uint8_t length_extra[29];
			static const uint16_t distance_base[30];
			static const uint8_t distance_extra[30];
			static const uint8_t code_length_order[19];
		};
		template <typename T> const uint16_t deflate_tables_t<T>::length_base[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
		template <typename T> const uint8_t deflate_tables_t<T>::length_extra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
		template <typename T> const uint16_t deflate_tables_t<T>::distance_base[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
		template <typename T> const uint8_t deflate_tables_t<T>::distance_extra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
		template <typename T> const uint8_t deflate_tables_t<T>::code_length_order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
		typedef deflate_tables_t<> deflate_tables;

		// Returns true at the end of the block, false when a literal or match finds the output full (zlib stops there)
		inline bool inflate_codes(inflate_state& s, const huffman_table& lencode, const huffman_table& distcode) {
			bytes& out = *s.out;
			for (;;) {
				int symbol = decode_symbol(s, lencode);
				if (symbol < 256) {
					if (out.size() >= s.room)
						return false;
					out.push_back(static_cast<uint8_t>(symbol));
					s.on_output();
					continue;
				}
				if (symbol == 256)
					return true;
				symbol -= 257;
				if (symbol >= 29)
					throw inflate_error("Invalid length code.");
				const size_t length = deflate_tables::length_base[symbol] + s.bits(deflate_tables::length_extra[symbol]);
				const int dsym = decode_symbol(s, distcode);
				if (dsym >= 30)
					throw inflate_error("Invalid distance code.");
				const size_t distance = deflate_tables::distance_base[dsym] + s.bits(deflate_tables::distance_extra[dsym]);
				if (out.size() >= s.room)
					return false;
				if (distance > out.size())
					throw inflate_error("Invalid distance (too far back).");
				const size_t copy = std::min(length, s.room - out.size());
				size_t from = out.size() - distance;
				for (size_t i = 0; i < copy; ++i) {
					const uint8_t value = out[from++];
					out.push_back(value);
				}
				s.on_output();
				if (copy < length)
					return false;
			}
		}

		inline bool inflate_stored(inflate_state& s) {
			const int drop = s.cnt % 8;
			s.buf >>= drop;
			s.cnt -= drop;
			const uint32_t len = s.bits(16);
			const uint32_t nlen = s.bits(16);
			if (len != (~nlen & 0xFFFFu))
				throw inflate_error("Invalid stored block lengths.");
			bytes& out = *s.out;
			size_t remaining = len;
			while (remaining > 0) {
				if (out.size() >= s.room)
					return false;
				if (s.cnt >= 8) {
					out.push_back(static_cast<uint8_t>(s.bits(8)));
				}
				else {
					if (s.pos >= s.length)
						throw inflate_input_end{};
					out.push_back(s.in[s.pos++]);
				}
				--remaining;
				s.on_output();
			}
			return true;
		}

		inline bool inflate_fixed(inflate_state& s) {
			struct fixed_tables {
				huffman_table lencode, distcode;
				fixed_tables() {
					uint8_t lengths[288];
					int symbol = 0;
					for (; symbol < 144; ++symbol) lengths[symbol] = 8;
					for (; symbol < 256; ++symbol) lengths[symbol] = 9;
					for (; symbol < 280; ++symbol) lengths[symbol] = 7;
					for (; symbol < 288; ++symbol) lengths[symbol] = 8;
					build_huffman(lencode, lengths, 288);
					// All 32 five-bit codes, as zlib builds them: 30 and 31 decode after 5 bits and are then refused
					// as invalid distance codes (with only 30 the code would look incomplete and need more input)
					for (symbol = 0; symbol < 32; ++symbol) lengths[symbol] = 5;
					build_huffman(distcode, lengths, 32);
				}
			};
			static const fixed_tables tables;
			return inflate_codes(s, tables.lencode, tables.distcode);
		}

		inline bool inflate_dynamic(inflate_state& s) {
			const int nlen = static_cast<int>(s.bits(5)) + 257;
			const int ndist = static_cast<int>(s.bits(5)) + 1;
			const int ncode = static_cast<int>(s.bits(4)) + 4;
			if (nlen > 286 || ndist > 30)
				throw inflate_error("Too many length or distance symbols.");
			uint8_t lengths[320];
			std::memset(lengths, 0, sizeof(lengths));
			for (int index = 0; index < ncode; ++index)
				lengths[deflate_tables::code_length_order[index]] = static_cast<uint8_t>(s.bits(3));
			huffman_table lencode, distcode;
			if (build_huffman(lencode, lengths, 19) != 0)
				throw inflate_error("Invalid code lengths code.");
			int index = 0;
			while (index < nlen + ndist) {
				int symbol = decode_symbol(s, lencode);
				if (symbol < 16) {
					lengths[index++] = static_cast<uint8_t>(symbol);
					continue;
				}
				uint8_t value = 0;
				int repeat;
				if (symbol == 16) {
					if (index == 0)
						throw inflate_error("Repeat with no first length.");
					value = lengths[index - 1];
					repeat = 3 + static_cast<int>(s.bits(2));
				}
				else if (symbol == 17) {
					repeat = 3 + static_cast<int>(s.bits(3));
				}
				else {
					repeat = 11 + static_cast<int>(s.bits(7));
				}
				if (index + repeat > nlen + ndist)
					throw inflate_error("Too many code lengths.");
				while (repeat-- > 0)
					lengths[index++] = value;
			}
			if (lengths[256] == 0)
				throw inflate_error("No end-of-block code.");
			int err = build_huffman(lencode, lengths, nlen);
			if (err != 0 && (err < 0 || nlen != lencode.count[0] + lencode.count[1]))
				throw inflate_error("Invalid literal/length code lengths.");
			err = build_huffman(distcode, lengths + nlen, ndist);
			if (err != 0 && (err < 0 || ndist != distcode.count[0] + distcode.count[1]))
				throw inflate_error("Invalid distance code lengths.");
			return inflate_codes(s, lencode, distcode);
		}

		// Inflates a zlib stream into at most `limit` bytes (see the notes on inflate_state). The Adler-32 trailer
		// is checked when the stream ends within the input zlib holds.
		inline bytes zlib_inflate(const uint8_t* data, size_t length, size_t limit) {
			bytes out;
			out.reserve(limit);
			const size_t room = limit + std::min(inflate_decode_ahead, static_cast<size_t>(-1) - limit);
			inflate_state s{ data, length, 0, 0, 0, &out, limit, room, false };
			try {
				const uint32_t cmf = s.bits(8);
				const uint32_t flg = s.bits(8);
				if ((cmf & 15) != 8 || (cmf >> 4) > 7 || ((cmf << 8) | flg) % 31 != 0)
					throw inflate_error("Invalid zlib header.");
				if ((flg & 0x20) != 0) {
					// zlib reads the 4-byte dictionary id first (running out of input there is not an error)
					for (int i = 0; i < 4; ++i)
						s.bits(8);
					throw inflate_need_dictionary();
				}
				bool last = false;
				while (!last) {
					last = s.bits(1) != 0;
					const uint32_t type = s.bits(2);
					bool finished;
					if (type == 0)
						finished = inflate_stored(s);
					else if (type == 1)
						finished = inflate_fixed(s);
					else if (type == 2)
						finished = inflate_dynamic(s);
					else
						throw inflate_error("Invalid block type.");
					if (!finished) {
						out.resize(std::min(out.size(), limit));
						return out;
					}
				}
				const int drop = s.cnt % 8;
				s.buf >>= drop;
				s.cnt -= drop;
				uint32_t check = 0;
				for (int i = 0; i < 4; ++i)
					check = (check << 8) | s.bits(8);
				if (out.size() <= limit && check != adler32(out.data(), out.size()))
					throw inflate_error("Incorrect data check.");
			}
			catch (const inflate_input_end&) {
			}
			out.resize(std::min(out.size(), limit));
			return out;
		}

		// ---- Deflate (RFC 1951) with a zlib wrapper (RFC 1950): LZ77 with hash chains and lazy matching,
		// each block written stored, with fixed codes or with dynamic codes, whichever is smallest ----
		struct bit_writer {
			bytes out;
			uint64_t buf = 0;
			int cnt = 0;
			void put(uint32_t value, int n) {
				buf |= static_cast<uint64_t>(value) << cnt;
				cnt += n;
				while (cnt >= 8) {
					out.push_back(static_cast<uint8_t>(buf));
					buf >>= 8;
					cnt -= 8;
				}
			}
			void align() {
				if (cnt > 0) {
					out.push_back(static_cast<uint8_t>(buf));
					buf = 0;
					cnt = 0;
				}
			}
		};

		struct deflate_token {
			uint16_t value;      // literal byte, or match length
			uint16_t distance;   // 0 for a literal
		};

		// Huffman code lengths no longer than `limit`; frequencies are halved until the tree fits
		inline std::vector<uint8_t> huffman_lengths(std::vector<uint32_t> freq, int limit) {
			const size_t n = freq.size();
			std::vector<uint8_t> lengths(n, 0);
			for (;;) {
				struct node { uint64_t weight; int left; int right; };   // leaf: left = -1, right = symbol
				std::vector<node> nodes;
				typedef std::pair<uint64_t, int> item;
				std::priority_queue<item, std::vector<item>, std::greater<item>> heap;
				for (size_t i = 0; i < n; ++i) {
					if (freq[i] != 0) {
						nodes.push_back(node{ freq[i], -1, static_cast<int>(i) });
						heap.push(item(freq[i], static_cast<int>(nodes.size() - 1)));
					}
				}
				if (nodes.empty())
					return lengths;
				if (nodes.size() == 1) {
					lengths[static_cast<size_t>(nodes[0].right)] = 1;
					return lengths;
				}
				while (heap.size() > 1) {
					const item a = heap.top();
					heap.pop();
					const item b = heap.top();
					heap.pop();
					nodes.push_back(node{ a.first + b.first, a.second, b.second });
					heap.push(item(a.first + b.first, static_cast<int>(nodes.size() - 1)));
				}
				std::fill(lengths.begin(), lengths.end(), static_cast<uint8_t>(0));
				int max_length = 0;
				std::vector<std::pair<int, int>> stack;
				stack.push_back(std::make_pair(heap.top().second, 0));
				while (!stack.empty()) {
					const std::pair<int, int> top = stack.back();
					stack.pop_back();
					const node& nd = nodes[static_cast<size_t>(top.first)];
					if (nd.left < 0) {
						lengths[static_cast<size_t>(nd.right)] = static_cast<uint8_t>(std::min(top.second, 255));
						max_length = std::max(max_length, top.second);
					}
					else {
						stack.push_back(std::make_pair(nd.left, top.second + 1));
						stack.push_back(std::make_pair(nd.right, top.second + 1));
					}
				}
				if (max_length <= limit)
					return lengths;
				for (uint32_t& f : freq)
					if (f != 0)
						f = (f + 1) / 2;
			}
		}

		// Canonical codes, bit-reversed for writing LSB first
		inline std::vector<uint16_t> canonical_codes(const std::vector<uint8_t>& lengths) {
			uint16_t bl_count[16] = { 0 };
			for (uint8_t l : lengths)
				if (l != 0)
					bl_count[l]++;
			uint16_t next_code[16] = { 0 };
			uint32_t code = 0;
			for (int bits = 1; bits <= 15; ++bits) {
				code = (code + bl_count[bits - 1]) << 1;
				next_code[bits] = static_cast<uint16_t>(code);
			}
			std::vector<uint16_t> codes(lengths.size(), 0);
			for (size_t i = 0; i < lengths.size(); ++i)
				if (lengths[i] != 0)
					codes[i] = static_cast<uint16_t>(reverse_bits(next_code[lengths[i]]++, lengths[i]));
			return codes;
		}

		inline int length_symbol(int length) {
			int code = 28;
			while (deflate_tables::length_base[code] > length)
				--code;
			return code;
		}

		inline int distance_symbol(int distance) {
			int code = 29;
			while (deflate_tables::distance_base[code] > distance)
				--code;
			return code;
		}

		inline std::vector<uint8_t> fixed_literal_lengths() {
			std::vector<uint8_t> lengths(288);
			for (size_t i = 0; i < 288; ++i)
				lengths[i] = static_cast<uint8_t>(i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8);
			return lengths;
		}

		inline void write_tokens(bit_writer& w, const std::vector<deflate_token>& tokens, size_t begin, size_t end,
			const std::vector<uint8_t>& ll_lengths, const std::vector<uint16_t>& ll_codes,
			const std::vector<uint8_t>& d_lengths, const std::vector<uint16_t>& d_codes) {
			for (size_t t = begin; t < end; ++t) {
				const deflate_token& token = tokens[t];
				if (token.distance == 0) {
					w.put(ll_codes[token.value], ll_lengths[token.value]);
					continue;
				}
				const int ls = length_symbol(token.value);
				w.put(ll_codes[static_cast<size_t>(257 + ls)], ll_lengths[static_cast<size_t>(257 + ls)]);
				w.put(static_cast<uint32_t>(token.value - deflate_tables::length_base[ls]), deflate_tables::length_extra[ls]);
				const int ds = distance_symbol(token.distance);
				w.put(d_codes[static_cast<size_t>(ds)], d_lengths[static_cast<size_t>(ds)]);
				w.put(static_cast<uint32_t>(token.distance - deflate_tables::distance_base[ds]), deflate_tables::distance_extra[ds]);
			}
			w.put(ll_codes[256], ll_lengths[256]);
		}

		inline void write_block(bit_writer& w, const std::vector<deflate_token>& tokens, const bytes& raw,
			size_t raw_begin, size_t raw_end, bool final_block) {
			std::vector<uint32_t> ll_freq(286, 0), d_freq(30, 0);
			for (const deflate_token& token : tokens) {
				if (token.distance == 0) {
					ll_freq[token.value]++;
				}
				else {
					ll_freq[static_cast<size_t>(257 + length_symbol(token.value))]++;
					d_freq[static_cast<size_t>(distance_symbol(token.distance))]++;
				}
			}
			ll_freq[256] = 1;

			// Bits each encoding needs, excluding the 3-bit block header
			uint64_t extra_bits = 0;
			for (const deflate_token& token : tokens)
				if (token.distance != 0)
					extra_bits += static_cast<uint64_t>(deflate_tables::length_extra[length_symbol(token.value)]) + deflate_tables::distance_extra[distance_symbol(token.distance)];

			const std::vector<uint8_t> fixed_ll = fixed_literal_lengths();
			const std::vector<uint8_t> fixed_d(30, 5);
			uint64_t fixed_bits = extra_bits;
			for (size_t i = 0; i < 286; ++i)
				fixed_bits += static_cast<uint64_t>(ll_freq[i]) * fixed_ll[i];
			for (size_t i = 0; i < 30; ++i)
				fixed_bits += static_cast<uint64_t>(d_freq[i]) * 5;

			// Dynamic codes: keep at least two codes in each tree so every decoder accepts it
			std::vector<uint32_t> ll_tree = ll_freq, d_tree = d_freq;
			if (std::count_if(ll_tree.begin(), ll_tree.end(), [](uint32_t f) { return f != 0; }) < 2)
				ll_tree[0] = std::max<uint32_t>(ll_tree[0], 1);
			if (d_tree[0] == 0)
				d_tree[0] = 1;
			if (std::count_if(d_tree.begin(), d_tree.end(), [](uint32_t f) { return f != 0; }) < 2)
				d_tree[1] = std::max<uint32_t>(d_tree[1], 1);
			const std::vector<uint8_t> ll_lengths = huffman_lengths(ll_tree, 15);
			const std::vector<uint8_t> d_lengths = huffman_lengths(d_tree, 15);
			size_t hlit = 286;
			while (hlit > 257 && ll_lengths[hlit - 1] == 0)
				--hlit;
			size_t hdist = 30;
			while (hdist > 1 && d_lengths[hdist - 1] == 0)
				--hdist;

			// Run-length encode the code lengths: (symbol, extra value, extra bits)
			std::vector<uint8_t> all(ll_lengths.begin(), ll_lengths.begin() + static_cast<std::ptrdiff_t>(hlit));
			all.insert(all.end(), d_lengths.begin(), d_lengths.begin() + static_cast<std::ptrdiff_t>(hdist));
			struct cl_symbol { uint8_t symbol; uint8_t extra; uint8_t extra_bits; };
			std::vector<cl_symbol> cl;
			for (size_t i = 0; i < all.size();) {
				const uint8_t value = all[i];
				size_t run = 1;
				while (i + run < all.size() && all[i + run] == value)
					++run;
				if (value == 0 && run >= 3) {
					const size_t take = std::min<size_t>(run, 138);
					if (take >= 11)
						cl.push_back(cl_symbol{ 18, static_cast<uint8_t>(take - 11), 7 });
					else
						cl.push_back(cl_symbol{ 17, static_cast<uint8_t>(take - 3), 3 });
					i += take;
				}
				else if (value != 0 && run >= 4) {
					cl.push_back(cl_symbol{ value, 0, 0 });
					const size_t take = std::min<size_t>(run - 1, 6);
					cl.push_back(cl_symbol{ 16, static_cast<uint8_t>(take - 3), 2 });
					i += 1 + take;
				}
				else {
					cl.push_back(cl_symbol{ value, 0, 0 });
					i += 1;
				}
			}
			std::vector<uint32_t> cl_freq(19, 0);
			for (const cl_symbol& c : cl)
				cl_freq[c.symbol]++;
			if (std::count_if(cl_freq.begin(), cl_freq.end(), [](uint32_t f) { return f != 0; }) < 2)
				cl_freq[cl_freq[0] == 0 ? 0 : 1] = 1;
			const std::vector<uint8_t> cl_lengths = huffman_lengths(cl_freq, 7);
			size_t hclen = 19;
			while (hclen > 4 && cl_lengths[deflate_tables::code_length_order[hclen - 1]] == 0)
				--hclen;

			uint64_t dynamic_bits = extra_bits + 5 + 5 + 4 + 3 * hclen;
			for (const cl_symbol& c : cl)
				dynamic_bits += static_cast<uint64_t>(cl_lengths[c.symbol]) + c.extra_bits;
			for (size_t i = 0; i < 286; ++i)
				dynamic_bits += static_cast<uint64_t>(ll_freq[i]) * ll_lengths[i];
			for (size_t i = 0; i < 30; ++i)
				dynamic_bits += static_cast<uint64_t>(d_freq[i]) * d_lengths[i];

			const size_t raw_length = raw_end - raw_begin;
			const uint64_t stored_bits = static_cast<uint64_t>(raw_length) * 8 + (raw_length / 65535 + 1) * (3 + 7 + 32);

			if (stored_bits <= fixed_bits && stored_bits <= dynamic_bits) {
				size_t at = raw_begin;
				do {
					const size_t chunk = std::min<size_t>(raw_end - at, 65535);
					const bool last_chunk = at + chunk == raw_end;
					w.put(final_block && last_chunk ? 1u : 0u, 1);
					w.put(0, 2);
					w.align();
					w.put(static_cast<uint32_t>(chunk), 16);
					w.put(static_cast<uint32_t>(~chunk & 0xFFFF), 16);
					w.out.insert(w.out.end(), raw.begin() + static_cast<std::ptrdiff_t>(at), raw.begin() + static_cast<std::ptrdiff_t>(at + chunk));
					at += chunk;
				} while (at < raw_end);
				return;
			}

			w.put(final_block ? 1u : 0u, 1);
			if (fixed_bits <= dynamic_bits) {
				w.put(1, 2);
				write_tokens(w, tokens, 0, tokens.size(), fixed_ll, canonical_codes(fixed_ll), fixed_d, canonical_codes(fixed_d));
				return;
			}
			w.put(2, 2);
			w.put(static_cast<uint32_t>(hlit - 257), 5);
			w.put(static_cast<uint32_t>(hdist - 1), 5);
			w.put(static_cast<uint32_t>(hclen - 4), 4);
			for (size_t i = 0; i < hclen; ++i)
				w.put(cl_lengths[deflate_tables::code_length_order[i]], 3);
			const std::vector<uint16_t> cl_codes = canonical_codes(cl_lengths);
			for (const cl_symbol& c : cl) {
				w.put(cl_codes[c.symbol], cl_lengths[c.symbol]);
				if (c.extra_bits != 0)
					w.put(c.extra, c.extra_bits);
			}
			write_tokens(w, tokens, 0, tokens.size(), ll_lengths, canonical_codes(ll_lengths), d_lengths, canonical_codes(d_lengths));
		}

		inline bytes zlib_deflate(const bytes& raw) {
			bit_writer w;
			w.out.push_back(0x78);
			w.out.push_back(0x9C);

			const size_t n = raw.size();
			const size_t window = 32768;
			const int max_chain = 128;
			std::vector<int32_t> head(1u << 15, -1);
			std::vector<int32_t> prev(window, -1);
			const auto hash3 = [&raw](size_t i) -> size_t {
				return ((static_cast<size_t>(raw[i]) << 10) ^ (static_cast<size_t>(raw[i + 1]) << 5) ^ raw[i + 2]) & 0x7FFF;
			};
			const auto insert = [&](size_t i) {
				if (i + 2 < n) {
					const size_t h = hash3(i);
					prev[i & (window - 1)] = head[h];
					head[h] = static_cast<int32_t>(i);
				}
			};
			const auto longest = [&](size_t i, int& best_distance) -> int {
				if (i + 2 >= n)
					return 0;
				const size_t max_length = std::min<size_t>(258, n - i);
				int best = 0;
				int32_t candidate = head[hash3(i)];
				for (int chain = 0; candidate >= 0 && chain < max_chain; ++chain) {
					const size_t c = static_cast<size_t>(candidate);
					const size_t distance = i - c;
					if (distance == 0 || distance >= window)
						break;
					if (raw[c + static_cast<size_t>(best)] == raw[i + static_cast<size_t>(best)]) {
						size_t length = 0;
						while (length < max_length && raw[c + length] == raw[i + length])
							++length;
						if (static_cast<int>(length) > best) {
							best = static_cast<int>(length);
							best_distance = static_cast<int>(distance);
							if (length == max_length)
								break;
						}
					}
					const int32_t next = prev[c & (window - 1)];
					if (next >= candidate)
						break;
					candidate = next;
				}
				return best >= 3 ? best : 0;
			};

			std::vector<deflate_token> tokens;
			tokens.reserve(65536);
			size_t block_begin = 0;
			size_t i = 0;
			while (i < n) {
				int distance = 0;
				const int length = longest(i, distance);
				if (length == 0) {
					insert(i);
					tokens.push_back(deflate_token{ raw[i], 0 });
					++i;
				}
				else {
					insert(i);
					int next_distance = 0;
					const int next_length = length < 258 && i + 1 < n ? longest(i + 1, next_distance) : 0;
					if (next_length > length) {
						tokens.push_back(deflate_token{ raw[i], 0 });
						++i;
					}
					else {
						tokens.push_back(deflate_token{ static_cast<uint16_t>(length), static_cast<uint16_t>(distance) });
						for (size_t k = 1; k < static_cast<size_t>(length); ++k)
							insert(i + k);
						i += static_cast<size_t>(length);
					}
				}
				if (tokens.size() >= 65536 && i < n) {
					write_block(w, tokens, raw, block_begin, i, false);
					tokens.clear();
					block_begin = i;
				}
			}
			write_block(w, tokens, raw, block_begin, n, true);
			w.align();
			put_u32be(w.out, adler32(raw.data(), raw.size()));
			return w.out;
		}

		// ---- Decoded image: top-down rows, 4 bytes (RGBA) per pixel ----
		struct image {
			int width = 0;
			int height = 0;
			bytes rgba;
		};
	}

	// ---------------------------------------------------------------------------------------------
	// SpriteImage
	// ---------------------------------------------------------------------------------------------
	namespace sprite_image {
		// Used for transparent pixels when a PNG becomes a BMP and the sprite has no color key
		constexpr uint32_t default_color_key = 0xFFFF00FFu;

		// Largest image width or height accepted (4K); also keeps a damaged header from exhausting memory
		constexpr int max_dimension = 4096;

		inline sprite_image_format get_format(const std::vector<uint8_t>& data) {
			static const uint8_t png_signature[8] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
			if (data.size() >= 8 && std::memcmp(data.data(), png_signature, 8) == 0)
				return sprite_image_format::png;
			if (data.size() >= 2 && data[0] == 0x42 && data[1] == 0x4D)
				return sprite_image_format::bmp;
			return sprite_image_format::unknown;
		}

		// Width and height from a BMP or PNG header, or false when the header is missing or too short
		inline bool try_get_size(const std::vector<uint8_t>& data, int& width, int& height) {
			width = height = 0;
			const sprite_image_format format = get_format(data);
			if (format == sprite_image_format::png && data.size() >= 24) {
				width = detail::i32be(&data[16]);
				height = detail::i32be(&data[20]);
				return true;
			}
			if (format == sprite_image_format::bmp && data.size() >= 26) {
				if (detail::i32le(&data[14]) == 12) {
					width = detail::u16le(&data[18]);
					height = std::abs(static_cast<int>(detail::i16le(&data[20])));
					return true;
				}
				width = detail::i32le(&data[18]);
				const long long h = std::llabs(static_cast<long long>(detail::i32le(&data[22])));
				height = static_cast<int>(std::min<long long>(h, 0x7FFFFFFFLL));
				return true;
			}
			return false;
		}

		inline std::string size_limit_message(long long width, long long height) {
			return "Image is " + detail::num(width) + "x" + detail::num(height) + "; the limit is " +
				detail::num(max_dimension) + "x" + detail::num(max_dimension) + ".";
		}

		// Throws invalid_data_error when the image is larger than max_dimension on either side
		inline void check_size(const std::vector<uint8_t>& data) {
			int width = 0, height = 0;
			if (try_get_size(data, width, height) && (width > max_dimension || height > max_dimension))
				throw invalid_data_error(size_limit_message(width, height));
		}

		// Bits per pixel of a BMP, or 0 when the header is too short to say
		inline int get_bmp_bits_per_pixel(const std::vector<uint8_t>& data) {
			if (get_format(data) != sprite_image_format::bmp || data.size() < 30)
				return 0;
			const int32_t dib_size = detail::i32le(&data[14]);
			return dib_size == 12 ? detail::u16le(&data[24]) : detail::u16le(&data[28]);
		}
	}

	// ---------------------------------------------------------------------------------------------
	// Implementation detail: BMP / PNG decoding and encoding
	// ---------------------------------------------------------------------------------------------
	namespace detail {
		inline image new_image(int64_t width, int64_t height) {
			if (width <= 0 || height <= 0)
				throw invalid_data_error("Unsupported image size " + num(width) + "x" + num(height) + ".");
			if (width > sprite_image::max_dimension || height > sprite_image::max_dimension)
				throw invalid_data_error(sprite_image::size_limit_message(width, height));
			image result;
			result.width = static_cast<int>(width);
			result.height = static_cast<int>(height);
			result.rgba.assign(static_cast<size_t>(width * height * 4), 0);
			return result;
		}

		inline uint8_t bmp_channel(uint32_t value, uint32_t mask) {
			if (mask == 0)
				return 0;
			int shift = 0;
			while (((mask >> shift) & 1u) == 0)
				++shift;
			const uint64_t max = mask >> shift;
			return static_cast<uint8_t>(static_cast<uint64_t>((value & mask) >> shift) * 255 / max);
		}

		// RLE8 / RLE4. set(x, rowFromBottom, index) is called for pixels inside the image.
		template <typename Set>
		inline void decode_bmp_rle(const bytes& d, int64_t offset, int w, int h, bool rle4, Set set) {
			int64_t x = 0, y = 0;
			int64_t p = offset;
			const int64_t length = static_cast<int64_t>(d.size());
			const auto put = [&](int index) {
				if (x < w && y < h)
					set(static_cast<int>(x), static_cast<int>(y), index);
				++x;
			};

			while (p + 1 < length && y < h) {
				const int count = d[static_cast<size_t>(p)];
				const int value = d[static_cast<size_t>(p + 1)];
				p += 2;
				if (count > 0) {
					for (int i = 0; i < count; ++i)
						put(rle4 ? (i % 2 == 0 ? value >> 4 : value & 0xF) : value);
				}
				else if (value == 0) {
					x = 0;      // end of line
					++y;
				}
				else if (value == 1) {
					break;      // end of bitmap
				}
				else if (value == 2) {
					if (p + 1 >= length)
						break;
					x += d[static_cast<size_t>(p)];
					y += d[static_cast<size_t>(p + 1)];
					p += 2;
				}
				else {
					// Absolute run of `value` pixels, padded to a 16-bit boundary
					const int count_bytes = rle4 ? (value + 1) / 2 : value;
					for (int i = 0; i < value && p + (rle4 ? i / 2 : i) < length; ++i) {
						const uint8_t b = d[static_cast<size_t>(p + (rle4 ? i / 2 : i))];
						put(rle4 ? (i % 2 == 0 ? b >> 4 : b & 0xF) : b);
					}
					p += (count_bytes + 1) & ~1;
				}
			}
		}

		inline image decode_bmp(const bytes& d) {
			if (d.size() < 26)
				throw invalid_data_error("BMP header is truncated.");
			const int64_t pixel_offset = u32le(&d[10]);
			const int32_t dib_size = i32le(&d[14]);
			int64_t width, height;
			int bits_per_pixel, palette_entry_size;
			int32_t compression = 0, colors_used = 0;
			if (dib_size == 12) {
				width = u16le(&d[18]);
				height = i16le(&d[20]);
				bits_per_pixel = u16le(&d[24]);
				palette_entry_size = 3;
			}
			else if (dib_size >= 40 && d.size() >= 14 + 40) {
				width = i32le(&d[18]);
				height = i32le(&d[22]);
				bits_per_pixel = u16le(&d[28]);
				compression = i32le(&d[30]);
				colors_used = i32le(&d[46]);
				palette_entry_size = 4;
			}
			else {
				throw invalid_data_error("Unsupported BMP header size " + num(dib_size) + ".");
			}

			const bool top_down = height < 0;
			if (bits_per_pixel != 1 && bits_per_pixel != 4 && bits_per_pixel != 8 && bits_per_pixel != 16 && bits_per_pixel != 24 && bits_per_pixel != 32)
				throw invalid_data_error("Unsupported BMP bit depth " + num(bits_per_pixel) + ".");
			if (compression != 0 && compression != 1 && compression != 2 && compression != 3 && compression != 6)
				throw invalid_data_error("Unsupported BMP compression " + num(compression) + ".");

			// Check the pixel data is all there before allocating anything (64-bit wrap-around as in C#)
			const int64_t abs_height = height < 0 ? -height : height;
			const int64_t stride = (static_cast<int64_t>(bits_per_pixel) * std::max<int64_t>(width, 0) + 31) / 32 * 4;
			const int64_t needed = wrap_i64(static_cast<uint64_t>(pixel_offset) + static_cast<uint64_t>(stride) * static_cast<uint64_t>(abs_height));
			if ((compression == 0 || compression == 3 || compression == 6) && needed > static_cast<int64_t>(d.size()))
				throw invalid_data_error("BMP pixel data is truncated.");

			image img = new_image(width, abs_height);
			const int w = img.width, h = img.height;

			// Palette for 1, 4 and 8 bpp
			std::vector<uint8_t> palette;   // RGB triples
			size_t palette_count = 0;
			if (bits_per_pixel <= 8) {
				palette_count = colors_used > 0 && colors_used <= 256 ? static_cast<size_t>(colors_used) : static_cast<size_t>(1) << bits_per_pixel;
				// SpriteImage computes 14 + dibSize in int: from 0x7FFFFFF2 up it wraps negative and indexing the
				// palette throws (IndexOutOfRangeException); here that is invalid data
				if (dib_size > 0x7FFFFFFF - 14)
					throw invalid_data_error("BMP header size " + num(dib_size) + " puts the palette outside the data.");
				const int64_t palette_offset = 14 + static_cast<int64_t>(dib_size) + (compression == 3 && dib_size == 40 ? 12 : 0);
				palette.assign(palette_count * 3, 0);
				for (size_t i = 0; i < palette_count; ++i) {
					const int64_t p = palette_offset + static_cast<int64_t>(i) * palette_entry_size;
					if (p + 3 <= static_cast<int64_t>(d.size())) {
						palette[i * 3] = d[static_cast<size_t>(p + 2)];
						palette[i * 3 + 1] = d[static_cast<size_t>(p + 1)];
						palette[i * 3 + 2] = d[static_cast<size_t>(p)];
					}
				}
			}

			const auto set_indexed = [&](int x, int y, int index) {
				const size_t o = (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4;
				const bool known = index >= 0 && static_cast<size_t>(index) < palette_count;
				img.rgba[o] = known ? palette[static_cast<size_t>(index) * 3] : 0;
				img.rgba[o + 1] = known ? palette[static_cast<size_t>(index) * 3 + 1] : 0;
				img.rgba[o + 2] = known ? palette[static_cast<size_t>(index) * 3 + 2] : 0;
				img.rgba[o + 3] = 255;
			};

			if (compression == 1 || compression == 2) {
				// Pixels the encoding skips (delta, early end of line) take palette index 0, as Windows does
				for (int y = 0; y < h; ++y)
					for (int x = 0; x < w; ++x)
						set_indexed(x, y, 0);
				decode_bmp_rle(d, pixel_offset, w, h, compression == 2,
					[&](int x, int row_from_bottom, int index) { set_indexed(x, h - 1 - row_from_bottom, index); });
				return img;
			}

			// Channel masks for 16 and 32 bpp
			uint32_t mask_r, mask_g, mask_b;
			if (compression == 3 || compression == 6) {
				const size_t mask_offset = 14 + 40;
				if (mask_offset + 12 > d.size())
					throw invalid_data_error("BMP bitfield masks are truncated.");
				mask_r = u32le(&d[mask_offset]);
				mask_g = u32le(&d[mask_offset + 4]);
				mask_b = u32le(&d[mask_offset + 8]);
			}
			else if (bits_per_pixel == 16) {
				mask_r = 0x7C00;
				mask_g = 0x03E0;
				mask_b = 0x001F;
			}
			else {
				mask_r = 0x00FF0000;
				mask_g = 0x0000FF00;
				mask_b = 0x000000FF;
			}

			for (int y = 0; y < h; ++y) {
				const size_t row = static_cast<size_t>(pixel_offset + static_cast<int64_t>(top_down ? y : h - 1 - y) * stride);
				for (int x = 0; x < w; ++x) {
					const size_t o = (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4;
					const size_t ux = static_cast<size_t>(x);
					switch (bits_per_pixel) {
					case 1: set_indexed(x, y, (d[row + ux / 8] >> (7 - ux % 8)) & 1); break;
					case 4: set_indexed(x, y, (d[row + ux / 2] >> (ux % 2 == 0 ? 4 : 0)) & 0xF); break;
					case 8: set_indexed(x, y, d[row + ux]); break;
					case 24:
						img.rgba[o] = d[row + ux * 3 + 2];
						img.rgba[o + 1] = d[row + ux * 3 + 1];
						img.rgba[o + 2] = d[row + ux * 3];
						img.rgba[o + 3] = 255;
						break;
					default: {
						const uint32_t v = bits_per_pixel == 16 ? u16le(&d[row + ux * 2]) : u32le(&d[row + ux * 4]);
						img.rgba[o] = bmp_channel(v, mask_r);
						img.rgba[o + 1] = bmp_channel(v, mask_g);
						img.rgba[o + 2] = bmp_channel(v, mask_b);
						img.rgba[o + 3] = 255;    // BMP alpha is dropped
						break;
					}
					}
				}
			}
			return img;
		}

		inline int paeth(int a, int b, int c) {
			const int p = a + b - c;
			const int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
			return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
		}

		inline void png_unfilter(uint8_t filter, const uint8_t* source, size_t length, const bytes& previous, bytes& current, size_t step) {
			for (size_t i = 0; i < length; ++i) {
				const int left = i >= step ? current[i - step] : 0;
				const int up = previous[i];
				const int up_left = i >= step ? previous[i - step] : 0;
				int predictor;
				switch (filter) {
				case 0: predictor = 0; break;
				case 1: predictor = left; break;
				case 2: predictor = up; break;
				case 3: predictor = (left + up) / 2; break;
				case 4: predictor = paeth(left, up, up_left); break;
				default: throw invalid_data_error("Unsupported PNG filter " + num(filter) + ".");
				}
				current[i] = static_cast<uint8_t>(source[i] + predictor);
			}
		}

		// Reads sample `index` of a row at the given bit depth, unscaled
		inline int png_sample(const bytes& row, int index, int bit_depth) {
			const size_t i = static_cast<size_t>(index);
			switch (bit_depth) {
			case 8: return row[i];
			case 16: return (row[i * 2] << 8) | row[i * 2 + 1];
			default: {
				const size_t bit = i * static_cast<size_t>(bit_depth);
				return (row[bit / 8] >> (8 - bit_depth - static_cast<int>(bit % 8))) & ((1 << bit_depth) - 1);
			}
			}
		}

		inline void png_write_pixel(image& img, int x, int y, const bytes& row, int column, int color_type, int bit_depth,
			int channels, const bytes& palette, const bytes& transparency) {
			const int max = (1 << bit_depth) - 1;
			const auto scale = [&](int value) { return static_cast<uint8_t>(bit_depth == 16 ? value >> 8 : value * 255 / max); };
			const auto trns_sample = [&](size_t i) {
				return transparency.size() >= i * 2 + 2 ? (transparency[i * 2] << 8) | transparency[i * 2 + 1] : -1;
			};

			const size_t o = (static_cast<size_t>(y) * static_cast<size_t>(img.width) + static_cast<size_t>(x)) * 4;
			const int s = column * channels;
			switch (color_type) {
			case 0: {
				const int g = png_sample(row, s, bit_depth);
				img.rgba[o] = img.rgba[o + 1] = img.rgba[o + 2] = scale(g);
				img.rgba[o + 3] = static_cast<uint8_t>(g == trns_sample(0) ? 0 : 255);
				break;
			}
			case 2: {
				const int r = png_sample(row, s, bit_depth), g = png_sample(row, s + 1, bit_depth), b = png_sample(row, s + 2, bit_depth);
				img.rgba[o] = scale(r);
				img.rgba[o + 1] = scale(g);
				img.rgba[o + 2] = scale(b);
				img.rgba[o + 3] = static_cast<uint8_t>(r == trns_sample(0) && g == trns_sample(1) && b == trns_sample(2) ? 0 : 255);
				break;
			}
			case 3: {
				const size_t index = static_cast<size_t>(png_sample(row, s, bit_depth));
				if (index * 3 + 2 < palette.size()) {
					img.rgba[o] = palette[index * 3];
					img.rgba[o + 1] = palette[index * 3 + 1];
					img.rgba[o + 2] = palette[index * 3 + 2];
				}
				img.rgba[o + 3] = index < transparency.size() ? transparency[index] : static_cast<uint8_t>(255);
				break;
			}
			case 4:
				img.rgba[o] = img.rgba[o + 1] = img.rgba[o + 2] = scale(png_sample(row, s, bit_depth));
				img.rgba[o + 3] = scale(png_sample(row, s + 1, bit_depth));
				break;
			default:
				img.rgba[o] = scale(png_sample(row, s, bit_depth));
				img.rgba[o + 1] = scale(png_sample(row, s + 1, bit_depth));
				img.rgba[o + 2] = scale(png_sample(row, s + 2, bit_depth));
				img.rgba[o + 3] = scale(png_sample(row, s + 3, bit_depth));
				break;
			}
		}

		inline image decode_png(const bytes& d) {
			int64_t position = 8;
			int width = 0, height = 0, bit_depth = 0, color_type = 0, interlace = 0;
			bytes palette, transparency, compressed;
			const int64_t size = static_cast<int64_t>(d.size());

			while (position + 12 <= size) {
				const int64_t length = u32be(&d[static_cast<size_t>(position)]);
				const std::string type = ascii(&d[static_cast<size_t>(position + 4)], 4);
				const int64_t data_start = position + 8;
				if (data_start + length > size)
					throw invalid_data_error("PNG chunk " + type + " is truncated.");
				const uint8_t* chunk = d.data() + data_start;
				const size_t chunk_length = static_cast<size_t>(length);

				if (type == "IHDR" && length >= 13) {
					width = i32be(chunk);
					height = i32be(chunk + 4);
					bit_depth = chunk[8];
					color_type = chunk[9];
					interlace = chunk[12];
				}
				else if (type == "PLTE") palette.assign(chunk, chunk + chunk_length);
				else if (type == "tRNS") transparency.assign(chunk, chunk + chunk_length);
				else if (type == "IDAT") compressed.insert(compressed.end(), chunk, chunk + chunk_length);
				else if (type == "IEND") break;

				position = data_start + length + 4;
			}

			int channels;
			switch (color_type) {
			case 0: channels = 1; break;
			case 2: channels = 3; break;
			case 3: channels = 1; break;
			case 4: channels = 2; break;
			case 6: channels = 4; break;
			default: throw invalid_data_error("Unsupported PNG color type " + num(color_type) + ".");
			}
			if (bit_depth != 1 && bit_depth != 2 && bit_depth != 4 && bit_depth != 8 && bit_depth != 16)
				throw invalid_data_error("Unsupported PNG bit depth " + num(bit_depth) + ".");
			image img = new_image(width, height);

			const int bits_per_pixel = channels * bit_depth;
			const size_t filter_step = static_cast<size_t>(std::max(1, bits_per_pixel / 8));
			static const int adam7[7][4] = { { 0, 0, 8, 8 }, { 4, 0, 8, 8 }, { 0, 4, 4, 8 }, { 2, 0, 4, 4 }, { 0, 2, 2, 4 }, { 1, 0, 2, 2 }, { 0, 1, 1, 2 } };
			static const int single[1][4] = { { 0, 0, 1, 1 } };
			const int(*passes)[4] = interlace == 1 ? adam7 : single;
			const int pass_count = interlace == 1 ? 7 : 1;

			// Inflate only as much as the image needs, so damaged data cannot expand without limit
			int64_t expected = 0;
			for (int p = 0; p < pass_count; ++p) {
				const int64_t pass_width = (width - passes[p][0] + passes[p][2] - 1) / passes[p][2];
				const int64_t pass_height = (height - passes[p][1] + passes[p][3] - 1) / passes[p][3];
				if (pass_width > 0 && pass_height > 0)
					expected += ((pass_width * bits_per_pixel + 7) / 8 + 1) * pass_height;
			}
			const bytes pixels = zlib_inflate(compressed.data(), compressed.size(), static_cast<size_t>(expected));

			size_t offset = 0;
			for (int p = 0; p < pass_count; ++p) {
				const int pass_width = (width - passes[p][0] + passes[p][2] - 1) / passes[p][2];
				const int pass_height = (height - passes[p][1] + passes[p][3] - 1) / passes[p][3];
				if (pass_width <= 0 || pass_height <= 0)
					continue;

				const size_t row_bytes = static_cast<size_t>((pass_width * bits_per_pixel + 7) / 8);
				bytes previous(row_bytes, 0);
				bytes current(row_bytes, 0);
				for (int row = 0; row < pass_height; ++row) {
					if (offset + 1 + row_bytes > pixels.size())
						throw invalid_data_error("PNG image data is truncated.");
					png_unfilter(pixels[offset], pixels.data() + offset + 1, row_bytes, previous, current, filter_step);
					offset += 1 + row_bytes;

					const int y = passes[p][1] + row * passes[p][3];
					for (int column = 0; column < pass_width; ++column) {
						const int x = passes[p][0] + column * passes[p][2];
						png_write_pixel(img, x, y, current, column, color_type, bit_depth, channels, palette, transparency);
					}
					previous.swap(current);
				}
			}
			return img;
		}

		// SpriteImage.Decode (internal in PAKLib)
		inline image decode(const bytes& data) {
			switch (sprite_image::get_format(data)) {
			case sprite_image_format::bmp: return decode_bmp(data);
			case sprite_image_format::png: return decode_png(data);
			default: throw invalid_data_error("Image data is neither BMP nor PNG.");
			}
		}

		inline void write_png_chunk(bytes& png, const char* type, const bytes& data) {
			put_u32be(png, static_cast<uint32_t>(data.size()));
			const uint8_t* type_bytes = reinterpret_cast<const uint8_t*>(type);
			png.insert(png.end(), type_bytes, type_bytes + 4);
			png.insert(png.end(), data.begin(), data.end());
			const uint32_t crc = crc32_update(crc32_update(0xFFFFFFFFu, type_bytes, 4), data.data(), data.size()) ^ 0xFFFFFFFFu;
			put_u32be(png, crc);
		}

		inline bytes encode_png(const image& img) {
			const size_t row_bytes = static_cast<size_t>(img.width) * 4;
			bytes raw((row_bytes + 1) * static_cast<size_t>(img.height), 0);
			for (size_t y = 0; y < static_cast<size_t>(img.height); ++y) {
				// Filter type 0 (none) per row
				std::memcpy(&raw[y * (row_bytes + 1) + 1], &img.rgba[y * row_bytes], row_bytes);
			}
			const bytes compressed = zlib_deflate(raw);

			bytes header;
			put_u32be(header, static_cast<uint32_t>(img.width));
			put_u32be(header, static_cast<uint32_t>(img.height));
			header.push_back(8);   // bit depth
			header.push_back(6);   // RGBA
			header.push_back(0);
			header.push_back(0);
			header.push_back(0);

			static const uint8_t png_signature[8] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
			bytes png(png_signature, png_signature + 8);
			write_png_chunk(png, "IHDR", header);
			write_png_chunk(png, "IDAT", compressed);
			write_png_chunk(png, "IEND", bytes());
			return png;
		}

		// key_used is the key applied to a BMP as 0xFFRRGGBB, or 0 (C# null) for a PNG
		inline bytes to_png(const bytes& data, uint32_t color_key, uint32_t& key_used) {
			key_used = 0;
			image img = decode(data);
			if (sprite_image::get_format(data) == sprite_image_format::bmp && img.width > 0 && img.height > 0) {
				uint8_t key_r, key_g, key_b;
				if (color_key != 0) {
					key_r = static_cast<uint8_t>(color_key >> 16);
					key_g = static_cast<uint8_t>(color_key >> 8);
					key_b = static_cast<uint8_t>(color_key);
				}
				else {
					key_r = img.rgba[0];
					key_g = img.rgba[1];
					key_b = img.rgba[2];
				}
				key_used = 0xFF000000u | (static_cast<uint32_t>(key_r) << 16) | (static_cast<uint32_t>(key_g) << 8) | key_b;
				for (size_t i = 0; i < img.rgba.size(); i += 4) {
					if (img.rgba[i] == key_r && img.rgba[i + 1] == key_g && img.rgba[i + 2] == key_b)
						img.rgba[i + 3] = 0;
				}
			}
			return encode_png(img);
		}
	}

	namespace sprite_image {
		// Converts a BMP or PNG to a 24bpp BMP. Pixels with alpha below 128 are filled with the color key
		// (or default_color_key when it is 0, C# null); BMP alpha is dropped.
		inline std::vector<uint8_t> to_bmp24(const std::vector<uint8_t>& data, uint32_t color_key = 0) {
			const detail::image img = detail::decode(data);
			const uint32_t key = color_key != 0 ? color_key : default_color_key;
			const uint8_t key_r = static_cast<uint8_t>(key >> 16), key_g = static_cast<uint8_t>(key >> 8), key_b = static_cast<uint8_t>(key);

			const size_t width = static_cast<size_t>(img.width), height = static_cast<size_t>(img.height);
			const size_t row_size = (width * 3 + 3) & ~static_cast<size_t>(3);
			std::vector<uint8_t> bmp(54 + row_size * height, 0);
			bmp[0] = 0x42;
			bmp[1] = 0x4D;
			detail::set_u32le(bmp, 2, static_cast<uint32_t>(bmp.size()));
			detail::set_u32le(bmp, 10, 54);
			detail::set_u32le(bmp, 14, 40);
			detail::set_u32le(bmp, 18, static_cast<uint32_t>(img.width));
			detail::set_u32le(bmp, 22, static_cast<uint32_t>(img.height));
			detail::set_u16le(bmp, 26, 1);
			detail::set_u16le(bmp, 28, 24);
			detail::set_u32le(bmp, 34, static_cast<uint32_t>(row_size * height));

			for (size_t y = 0; y < height; ++y) {
				// BMP rows are stored bottom-up
				const size_t row = 54 + (height - 1 - y) * row_size;
				for (size_t x = 0; x < width; ++x) {
					const size_t src = (y * width + x) * 4;
					const size_t dst = row + x * 3;
					const bool transparent = img.rgba[src + 3] < 128;
					bmp[dst] = transparent ? key_b : img.rgba[src + 2];
					bmp[dst + 1] = transparent ? key_g : img.rgba[src + 1];
					bmp[dst + 2] = transparent ? key_r : img.rgba[src];
				}
			}
			return bmp;
		}

		// Converts a BMP or PNG to an 8-bit RGBA PNG. For a BMP, pixels matching the color key (or, when it is 0,
		// C# null, the top-left pixel, as the game client does) become transparent.
		inline std::vector<uint8_t> to_png(const std::vector<uint8_t>& data, uint32_t color_key = 0) {
			uint32_t key_used = 0;
			return detail::to_png(data, color_key, key_used);
		}
	}

	// ---------------------------------------------------------------------------------------------
	// Sprite
	// ---------------------------------------------------------------------------------------------
	inline sprite_image_format sprite::image_format() const {
		return sprite_image::get_format(image_data);
	}

	inline void sprite::convert_to_bmp() {
		if (image_format() == sprite_image_format::png && !has_color_key())
			set_color_key(sprite_image::default_color_key);
		image_data = sprite_image::to_bmp24(image_data, color_key);
	}

	inline void sprite::convert_to_png() {
		uint32_t key_used = 0;
		image_data = detail::to_png(image_data, color_key, key_used);
		if (!has_color_key())
			set_color_key(key_used);
	}

	// ---------------------------------------------------------------------------------------------
	// PAKData: read, write, convert
	// ---------------------------------------------------------------------------------------------
	namespace detail {
		inline bool is_valid_bmp(const bytes& data) {
			// PAKLib reads the header fields of a 14-25 byte "BMP" past its end; 26 bytes are required here
			if (data.size() < 26)
				return false;
			if (data[0] != 0x42 || data[1] != 0x4D)
				return false;
			const int32_t declared_size = i32le(&data[2]);
			if (declared_size > static_cast<int64_t>(data.size()))
				return false;
			const int32_t width = i32le(&data[18]);
			const int32_t height = i32le(&data[22]);
			return width > 0 && height > 0;
		}

		inline bool is_valid_png(const bytes& data) {
			static const uint8_t png_signature[8] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
			if (data.size() < 8 || std::memcmp(data.data(), png_signature, 8) != 0)
				return false;
			if (data.size() < 24)
				return false;
			if (i32be(&data[8]) != 13)
				return false;
			return std::memcmp(&data[12], "IHDR", 4) == 0;
		}

		// Images are capped at sprite_image::max_dimension (4096) per side
		inline void check_image_size(const bytes& image_bytes, size_t index) {
			try {
				sprite_image::check_size(image_bytes);
			}
			catch (const invalid_data_error& ex) {
				throw invalid_data_error("Sprite " + num(static_cast<long long>(index)) + ": " + ex.what());
			}
		}

		// BMPs are always written as 24bpp; other bit depths are converted on the way out (the sprite is not changed).
		// Returns the bytes to write: either &s.image_data or &converted.
		inline const bytes* image_for_writing(const sprite& s, size_t index, bytes& converted) {
			check_image_size(s.image_data, index);
			if (s.image_format() != sprite_image_format::bmp || sprite_image::get_bmp_bits_per_pixel(s.image_data) == 24)
				return &s.image_data;
			try {
				converted = sprite_image::to_bmp24(s.image_data, s.get_color_key());
			}
			catch (const invalid_data_error& ex) {
				throw invalid_data_error("Sprite " + num(static_cast<long long>(index)) + ": cannot convert BMP to 24bpp. " + ex.what());
			}
			return &converted;
		}
	}

	namespace detail {
		// Byte sources for the strict reader: the whole PAK in memory, or a file read on demand (get_sprite)
		struct memory_source {
			const bytes& data;
			int64_t size() const { return static_cast<int64_t>(data.size()); }
			void read(int64_t pos, int64_t count, uint8_t* destination) const {
				if (count > 0)
					std::memcpy(destination, data.data() + pos, static_cast<size_t>(count));
			}
		};

		struct file_source {
			std::ifstream& file;
			int64_t length;
			const std::string& path;
			int64_t size() const { return length; }
			void read(int64_t pos, int64_t count, uint8_t* destination) const {
				if (count <= 0)
					return;
				file.clear();
				file.seekg(static_cast<std::streamoff>(pos), std::ios::beg);
				file.read(reinterpret_cast<char*>(destination), static_cast<std::streamsize>(count));
				if (!file)
					throw std::runtime_error("Failed to read PAK file: " + path);
			}
		};

		inline void need_bytes(int64_t length, int64_t pos, int64_t count, const char* what) {
			if (length - pos < count)
				throw invalid_data_error("Unexpected end of data at offset " + num(pos) + " while reading " + what + ".");
		}

		// A byte count already checked against the data left can still exceed size_t when a file source is
		// larger than the address space (32-bit builds); such a size cannot be allocated, so it does not fit either
		inline bool fits_size_t(int64_t n) {
			return static_cast<uint64_t>(n) <= static_cast<uint64_t>(static_cast<size_t>(-1));
		}

		// File header magic and padding, then the sprite count and table (PAKData.Read up to the first sprite).
		// strict = false (the fast readers) skips the magic check but keeps every count and bounds rule.
		template <typename Source>
		inline int64_t read_header_and_table(const Source& src, pak& result, bool strict = true) {
			const int64_t length = src.size();
			int64_t pos = 0;

			// File header magic, then 3 padding bytes
			uint8_t magic[file_header_size];
			if (length < static_cast<int64_t>(file_header_size)) {
				if (strict)
					throw invalid_data_error("Invalid PAK file header magic.");
				src.read(0, length, reinterpret_cast<uint8_t*>(result.header.signature));
				pos = length;
			}
			else {
				src.read(0, static_cast<int64_t>(file_header_size), magic);
				if (strict && std::memcmp(magic, detail::magic::file, file_header_size) != 0)
					throw invalid_data_error("Invalid PAK file header magic.");
				std::memcpy(result.header.signature, magic, file_header_size);
				pos = static_cast<int64_t>(file_header_size);
				const int64_t padding = std::min<int64_t>(3, length - pos);
				src.read(pos, padding, result.header.padding);
				pos += padding;
			}

			// Sprite count, then the table of (offset, length) pairs
			need_bytes(length, pos, 4, "the sprite count");
			uint8_t field[4];
			src.read(pos, 4, field);
			const int32_t count = i32le(field);
			pos += 4;
			if (count < 0)
				throw invalid_data_error("Sprite count " + num(count) + " is negative.");
			if (static_cast<int64_t>(count) * 8 > length - pos || !fits_size_t(static_cast<int64_t>(count) * 8))
				throw invalid_data_error("Sprite count " + num(count) + " does not fit in the data: the sprite table would run past the end.");
			bytes table(static_cast<size_t>(count) * 8);
			src.read(pos, static_cast<int64_t>(table.size()), table.data());
			result.sprite_count = static_cast<uint32_t>(count);
			result.sprite_entries.reserve(static_cast<size_t>(count));
			for (size_t i = 0; i < static_cast<size_t>(count); ++i) {
				const int32_t offset = i32le(&table[i * 8]);
				const int32_t size = i32le(&table[i * 8 + 4]);
				if (offset < 0 || size < 0)
					throw invalid_data_error("Sprite table entry " + num(static_cast<long long>(i)) + " has a negative offset (" + num(offset) + ") or length (" + num(size) + ").");
				sprite_entry entry;
				entry.offset = static_cast<uint32_t>(offset);
				entry.size = static_cast<uint32_t>(size);
				result.sprite_entries.push_back(entry);
			}
			return pos + static_cast<int64_t>(table.size());
		}

		// One sprite entry starting at pos (PAKData.Read's loop body, after the position check); returns the end position.
		// strict = false (the fast readers) skips the 100-byte header unchecked and the BMP / PNG and size checks,
		// but keeps the same count and bounds rules.
		template <typename Source>
		inline int64_t read_sprite_entry(const Source& src, int64_t pos, int64_t chunk_offset, int64_t chunk_size, size_t index, sprite& s, bool strict = true) {
			const int64_t length = src.size();
			const std::string sprite_name = "Sprite " + num(static_cast<long long>(index));

			if (strict) {
				// The magic is required because PAKLib always writes it (some original files, e.g. MHelm3.pak and
				// MHelm5.pak, have it zeroed and are rejected). The padding is not checked: original files contain
				// leftover data there.
				if (length - pos < static_cast<int64_t>(sprite_header_size))
					throw invalid_data_error("Invalid sprite header magic for sprite " + num(static_cast<long long>(index)) + " at offset " + num(chunk_offset) + ".");
				src.read(pos, static_cast<int64_t>(sprite_header_size), reinterpret_cast<uint8_t*>(s.header.signature));
				if (std::memcmp(s.header.signature, detail::magic::sprite, sprite_header_size) != 0)
					throw invalid_data_error("Invalid sprite header magic for sprite " + num(static_cast<long long>(index)) + " at offset " + num(chunk_offset) + ".");
				pos += static_cast<int64_t>(sprite_header_size);
				const int64_t header_padding = std::min<int64_t>(80, length - pos);
				src.read(pos, header_padding, s.header.padding);
				pos += header_padding;
			}
			else {
				// The 100-byte header is kept as read, unchecked; a cut header ends where the data does, as in strict mode
				const int64_t present = std::max<int64_t>(0, std::min<int64_t>(static_cast<int64_t>(sizeof(sprite_header)), length - pos));
				src.read(pos, present, reinterpret_cast<uint8_t*>(&s.header));
				pos += present;
			}

			// Rectangles
			uint8_t field[4];
			need_bytes(length, pos, 4, "the rectangle count");
			src.read(pos, 4, field);
			const int32_t rect_count = i32le(field);
			pos += 4;
			if (rect_count < 0)
				throw invalid_data_error(sprite_name + ": rectangle count " + num(rect_count) + " is negative.");
			if (static_cast<int64_t>(rect_count) * 12 > length - pos || !fits_size_t(static_cast<int64_t>(rect_count) * 12))
				throw invalid_data_error(sprite_name + ": rectangle count " + num(rect_count) + " does not fit in the data.");
			bytes records(static_cast<size_t>(rect_count) * 12);
			src.read(pos, static_cast<int64_t>(records.size()), records.data());
			pos += static_cast<int64_t>(records.size());
			s.sprite_rectangles.resize(static_cast<size_t>(rect_count));
			for (size_t j = 0; j < s.sprite_rectangles.size(); ++j) {
				const uint8_t* r = &records[j * 12];
				sprite_rect& rect = s.sprite_rectangles[j];
				rect.x = u16le(r);
				rect.y = u16le(r + 2);
				rect.width = u16le(r + 4);
				rect.height = u16le(r + 6);
				rect.pivotX = i16le(r + 8);
				rect.pivotY = i16le(r + 10);
			}

			// Transparency color key, stored as bytes A RR GG BB (alpha other than FF = none authored)
			need_bytes(length, pos, 4, "the color key");
			src.read(pos, 4, field);
			s.set_color_key(u32be(field));
			pos += 4;

			// The image fills the rest of the entry
			const int64_t image_length = chunk_size - (static_cast<int64_t>(sprite_header_size) + 80 + (static_cast<int64_t>(rect_count) * 12 + 4)) - 4;
			if (image_length < 0)
				throw invalid_data_error(sprite_name + ": image length " + num(image_length) + " is negative.");
			if (image_length > length - pos || !fits_size_t(image_length))
				throw invalid_data_error(sprite_name + ": image length " + num(image_length) + " runs past the end of the data.");
			s.image_data.resize(static_cast<size_t>(image_length));
			src.read(pos, image_length, s.image_data.data());
			pos += image_length;

			if (strict) {
				if (!is_valid_bmp(s.image_data) && !is_valid_png(s.image_data))
					throw invalid_data_error("Invalid sprite data format. Expected BMP or PNG.");
				check_image_size(s.image_data, index);
			}
			return pos;
		}

		// PAK.ReadFromFile's path checks (the absolute-path check is not ported: relative paths are accepted)
		inline void check_read_path(const std::string& filepath) {
			if (is_null_or_white_space(filepath))
				throw std::invalid_argument("File path cannot be null or empty.");
			if (!file_exists(filepath))
				throw file_not_found_error("The specified file does not exist.", filepath);
			if (lower_extension(filepath) == U".epak")
				throw not_supported_error("Encrypted .epak files are no longer supported (removed in PAKLib 2.0).");
		}
	}

	inline pak pak::read(const std::vector<uint8_t>& bytes) {
		using namespace detail;
		const memory_source src{ bytes };
		pak result{};
		int64_t pos = read_header_and_table(src, result);

		result.sprites.reserve(result.sprite_entries.size());
		for (size_t i = 0; i < result.sprite_entries.size(); ++i) {
			const int64_t chunk_offset = result.sprite_entries[i].offset;
			const int64_t chunk_size = result.sprite_entries[i].size;
			if (pos != chunk_offset)
				throw std::runtime_error("Expected to be at position " + num(chunk_offset) + ", but was at " + num(pos) + ".");
			sprite s;
			pos = read_sprite_entry(src, pos, chunk_offset, chunk_size, i, s);
			result.sprites.push_back(std::move(s));
		}

		return result;
	}

	inline std::vector<uint8_t> pak::to_bytes() const {
		using namespace detail;
		bytes out;

		// Default file header
		out.insert(out.end(), detail::magic::file, detail::magic::file + file_header_size);
		out.insert(out.end(), 3, 0);

		// Sprite count, then a placeholder table filled in as each sprite is written
		put_u32le(out, static_cast<uint32_t>(sprites.size()));
		const size_t table = out.size();
		out.insert(out.end(), sprites.size() * 8, 0);

		for (size_t i = 0; i < sprites.size(); ++i) {
			const sprite& s = sprites[i];
			bytes converted;
			const bytes& image_bytes = *image_for_writing(s, i, converted);

			const size_t start = out.size();
			out.insert(out.end(), detail::magic::sprite, detail::magic::sprite + sprite_header_size);
			out.insert(out.end(), 80, 0);
			put_u32le(out, static_cast<uint32_t>(s.sprite_rectangles.size()));
			for (const sprite_rect& rect : s.sprite_rectangles) {
				put_u16le(out, rect.x);
				put_u16le(out, rect.y);
				put_u16le(out, rect.width);
				put_u16le(out, rect.height);
				put_u16le(out, static_cast<uint16_t>(rect.pivotX));
				put_u16le(out, static_cast<uint16_t>(rect.pivotY));
			}
			// Transparency color key, stored as bytes A RR GG BB (00 00 00 00 = none authored)
			put_u32be(out, s.get_color_key());
			const size_t header_bytes = out.size() - start;
			out.insert(out.end(), image_bytes.begin(), image_bytes.end());

			set_u32le(out, table + i * 8, static_cast<uint32_t>(start));
			set_u32le(out, table + i * 8 + 4, static_cast<uint32_t>(image_bytes.size() + header_bytes));
		}
		return out;
	}

	inline void pak::write(const std::string& file_path) const {
		detail::write_all_bytes(file_path, to_bytes());
	}

	inline void pak::convert_images(sprite_image_format format) {
		if (format == sprite_image_format::unknown)
			throw std::invalid_argument("Target format must be Bmp or Png.");
		for (sprite& s : sprites) {
			if (format == sprite_image_format::bmp)
				s.convert_to_bmp();
			else
				s.convert_to_png();
		}
	}

	// ---------------------------------------------------------------------------------------------
	// PAK: file-level operations
	// ---------------------------------------------------------------------------------------------

	// PAK.ReadFromFile. The path may be relative or absolute and is opened exactly as given.
	inline pak loadpak(const std::string& filepath) {
		detail::check_read_path(filepath);
		return pak::read(detail::read_all_bytes(filepath));
	}

	// One sprite (no PAKLib counterpart): PAK.ReadFromFile's path checks, the file header, sprite count and table
	// checks, then PAKData.Read's checks for that sprite (magic, bounds, BMP/PNG, the 4096 cap). Only the header,
	// the table and the one sprite are read, so the position check that chains sprites in PAKData.Read is not made.
	inline sprite get_sprite(const std::string& filepath, size_t sprite_index) {
		detail::check_read_path(filepath);
		std::ifstream file(filepath, std::ios::binary);
		if (!file.is_open())
			throw std::runtime_error("Failed to open PAK file: " + filepath);
		file.seekg(0, std::ios::end);
		const detail::file_source src{ file, static_cast<int64_t>(file.tellg()), filepath };

		pak table{};
		detail::read_header_and_table(src, table);
		if (sprite_index >= table.sprite_entries.size())
			throw argument_out_of_range_error("Sprite index out of bounds in PAK file: " + filepath);

		const sprite_entry& entry = table.sprite_entries[sprite_index];
		sprite result;
		detail::read_sprite_entry(src, entry.offset, entry.offset, entry.size, sprite_index, result);
		return result;
	}

	// PAK.SaveToFile
	inline void save_to_file(const pak& pak_file, const std::string& filepath) {
		if (detail::is_null_or_white_space(filepath))
			throw std::invalid_argument("File path cannot be null or empty.");
		pak_file.write(filepath);
	}

	// ---------------------------------------------------------------------------------------------
	// PAKRepair
	// ---------------------------------------------------------------------------------------------
	namespace detail {
		constexpr int64_t repair_sprite_count_offset = 20;
		constexpr int64_t repair_sprite_table_offset = 24;
		constexpr int64_t repair_table_entry_size = 8;
		constexpr int64_t repair_sprite_header_size = 100;
		constexpr int64_t repair_min_sprite_entry_size = repair_sprite_header_size + 4 + 4; // header + rectangle count + color key
		constexpr int64_t repair_rectangle_size = 12;
		constexpr int64_t repair_wide_rectangle_size = 16;
		constexpr int64_t repair_max_rectangles = 65535;
		constexpr int repair_placeholder_size = 32;

		struct repair_note {
			int64_t offset;
			std::string text;
		};

		struct repair_slot {
			int64_t position = 0;            // start of the sprite entry, or of its image when the entry is lost
			int64_t end = 0;                 // end of the bytes this sprite claims
			bool has_sprite = false;         // false when the sprite is corrupt
			sprite spr;
			std::string problem;
			int rectangle_count_hint = -1;   // frame count to give a placeholder, when known
			std::vector<repair_note> notes;
		};

		struct repair_probe {
			bool is_image = false;
			bool complete = false;
			int64_t length = 0;              // full length when complete, otherwise the bytes available
			bool size_field_damaged = false; // BMP size field runs past the data; length comes from the pixel layout
		};

		typedef std::vector<std::pair<int64_t, int64_t>> repair_table;
		typedef std::unordered_map<int64_t, int64_t> repair_lengths;

		inline void add_issue(repair_report& report, int sprite_index, int64_t offset, const std::string& description, bool repaired) {
			repair_issue issue;
			issue.sprite_index = sprite_index;
			issue.offset = offset;
			issue.description = description;
			issue.repaired = repaired;
			report.issues.push_back(issue);
		}

		inline bool looks_like_sprite_start(const bytes& b, int64_t position) {
			return position >= 0 && position + static_cast<int64_t>(sprite_header_size) <= static_cast<int64_t>(b.size()) &&
				std::memcmp(&b[static_cast<size_t>(position)], detail::magic::sprite, sprite_header_size) == 0;
		}

		// First sprite header magic wholly inside [from, to), or -1
		inline int64_t find_sprite_magic(const bytes& b, int64_t from, int64_t to) {
			if (from < 0 || from >= to)
				return -1;
			const int64_t last = to - static_cast<int64_t>(sprite_header_size);
			for (int64_t at = from; at <= last;) {
				const void* hit = std::memchr(&b[static_cast<size_t>(at)], '<', static_cast<size_t>(last - at + 1));
				if (hit == nullptr)
					return -1;
				at = static_cast<const uint8_t*>(hit) - b.data();
				if (std::memcmp(&b[static_cast<size_t>(at)], detail::magic::sprite, sprite_header_size) == 0)
					return at;
				++at;
			}
			return -1;
		}

		// The last value in sorted that is below position and at least minimum, or -1
		inline int64_t last_before(const std::vector<int64_t>& sorted, int64_t position, int64_t minimum) {
			const std::ptrdiff_t index = (std::lower_bound(sorted.begin(), sorted.end(), position) - sorted.begin()) - 1;
			return index >= 0 && sorted[static_cast<size_t>(index)] >= minimum ? sorted[static_cast<size_t>(index)] : -1;
		}

		inline int64_t incomplete_end(const bytes& b, int64_t image_start, int64_t to) {
			const int64_t next_header = find_sprite_magic(b, image_start + 8, to);
			return next_header >= 0 ? next_header : to;
		}

		// Index of the slot whose bytes contain position, in slots sorted by position, or -1
		inline int find_containing(const std::vector<repair_slot>& sorted, int64_t position) {
			int low = 0, high = static_cast<int>(sorted.size()) - 1, found = -1;
			while (low <= high) {
				const int mid = (low + high) / 2;
				if (sorted[static_cast<size_t>(mid)].position <= position) {
					found = mid;
					low = mid + 1;
				}
				else {
					high = mid - 1;
				}
			}
			if (found < 0)
				return -1;
			const repair_slot& slot = sorted[static_cast<size_t>(found)];
			return position < std::max(slot.end, slot.position + 1) ? found : -1;
		}

		inline void sort_slots(std::vector<repair_slot>& slots) {
			std::stable_sort(slots.begin(), slots.end(), [](const repair_slot& a, const repair_slot& b) { return a.position < b.position; });
		}

		// Reads the sprite table. Returns no entries when the table cannot be trusted.
		inline repair_table read_repair_table(const bytes& b, repair_report* report, int64_t& table_end, bool& table_readable) {
			repair_table table;
			table_readable = false;
			const int64_t size = static_cast<int64_t>(b.size());
			if (size < repair_sprite_table_offset) {
				table_end = 0;
				if (report != nullptr)
					add_issue(*report, -1, 0, "File is " + num(size) + " bytes, too short for a PAK header.", false);
				return table;
			}

			if (report != nullptr && std::memcmp(b.data(), detail::magic::file, file_header_size) != 0)
				add_issue(*report, -1, 0, "File header magic was invalid; rebuilt.", true);

			const int32_t count = i32le(&b[static_cast<size_t>(repair_sprite_count_offset)]);
			if (count < 0 || repair_sprite_table_offset + static_cast<int64_t>(count) * repair_table_entry_size > size) {
				table_end = repair_sprite_table_offset;
				if (report != nullptr)
					add_issue(*report, -1, repair_sprite_count_offset, "Sprite count " + num(count) + " does not fit in the file; table rebuilt from the sprites found.", true);
				return table;
			}

			for (int32_t i = 0; i < count; ++i) {
				const size_t entry = static_cast<size_t>(repair_sprite_table_offset + static_cast<int64_t>(i) * repair_table_entry_size);
				table.push_back(std::make_pair(static_cast<int64_t>(i32le(&b[entry])), static_cast<int64_t>(i32le(&b[entry + 4]))));
			}
			table_end = repair_sprite_table_offset + static_cast<int64_t>(count) * repair_table_entry_size;
			table_readable = true;
			return table;
		}

		inline repair_probe probe_image(const bytes& b, int64_t position, int64_t to) {
			const repair_probe none{};
			if (position + 8 > to)
				return none;
			const size_t p = static_cast<size_t>(position);

			// PNG: signature, IHDR first, then walk the chunks to IEND
			if (b[p] == 0x89 && b[p + 1] == 0x50) {
				static const uint8_t png_signature[8] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
				if (std::memcmp(&b[p], png_signature, 8) != 0)
					return none;
				int64_t chunk = position + 8;
				if (chunk + 8 > to || u32be(&b[static_cast<size_t>(chunk)]) != 13 || std::memcmp(&b[static_cast<size_t>(chunk + 4)], "IHDR", 4) != 0)
					return none;

				while (chunk + 12 <= to) {
					const int64_t next = chunk + 12 + static_cast<int64_t>(u32be(&b[static_cast<size_t>(chunk)]));
					if (std::memcmp(&b[static_cast<size_t>(chunk + 4)], "IEND", 4) == 0) {
						repair_probe found;
						found.is_image = true;
						found.complete = next <= to;
						found.length = next <= to ? next - position : to - position;
						return found;
					}
					chunk = next;
				}
				repair_probe partial;
				partial.is_image = true;
				partial.length = to - position;
				return partial;
			}

			// BMP: file header plus a believable DIB header, then the length from the pixel layout
			if (b[p] == 0x42 && b[p + 1] == 0x4D) {
				if (position + 30 > to)
					return none;
				const int64_t declared_size = u32le(&b[p + 2]);
				const int64_t pixel_offset = u32le(&b[p + 10]);
				const int32_t dib_size = i32le(&b[p + 14]);
				int64_t width, height;
				int planes, bits_per_pixel;
				int32_t compression = 0;
				if (dib_size == 12) {
					width = u16le(&b[p + 18]);
					height = i16le(&b[p + 20]);
					planes = u16le(&b[p + 22]);
					bits_per_pixel = u16le(&b[p + 24]);
				}
				else if (dib_size == 40 || dib_size == 52 || dib_size == 56 || dib_size == 108 || dib_size == 124) {
					if (position + 34 > to)
						return none;
					width = i32le(&b[p + 18]);
					height = i32le(&b[p + 22]);
					planes = u16le(&b[p + 26]);
					bits_per_pixel = u16le(&b[p + 28]);
					compression = i32le(&b[p + 30]);
				}
				else {
					return none;
				}

				// Height must be positive: the reader rejects top-down bitmaps
				const bool header_valid = planes == 1
					&& (bits_per_pixel == 1 || bits_per_pixel == 4 || bits_per_pixel == 8 || bits_per_pixel == 16 || bits_per_pixel == 24 || bits_per_pixel == 32)
					&& width > 0 && width <= 32768 && height > 0 && height <= 32768
					&& pixel_offset >= 14 + dib_size && pixel_offset <= 14 + dib_size + 1024 + 16;
				if (!header_valid)
					return none;

				int64_t length;
				if (compression == 0 || compression == 3) {
					const int64_t row_size = (bits_per_pixel * width + 31) / 32 * 4;
					length = pixel_offset + row_size * height;

					// Keep extra bytes a larger size field claims, unless that size runs past the data: then the
					// size field is damaged and the pixel layout decides
					if (declared_size > length) {
						if (position + declared_size <= to) {
							length = declared_size;
						}
						else if (position + length <= to) {
							repair_probe damaged;
							damaged.is_image = true;
							damaged.complete = true;
							damaged.length = length;
							damaged.size_field_damaged = true;
							return damaged;
						}
					}
				}
				else if (declared_size > pixel_offset) {
					length = declared_size;
				}
				else {
					return none;
				}

				repair_probe found;
				found.is_image = true;
				found.complete = position + length <= to;
				found.length = found.complete ? length : to - position;
				return found;
			}

			return none;
		}

		// Works out where the sprite entry for the image at image_start begins, by trying each possible frame count
		// and weighing the evidence: a matching rectangle count, the header magic, a sprite table entry, and the
		// entry starting where the previous sprite ended.
		inline bool try_find_entry_start(const bytes& b, int64_t image_start, int64_t previous_end, int64_t earliest_start,
			const repair_lengths& table_lengths, int64_t& start, int& rectangle_count, bool& count_recovered, bool& wide) {
			start = -1;
			rectangle_count = 0;
			count_recovered = false;
			wide = false;
			int best_score = 0;

			const int64_t available = image_start - repair_min_sprite_entry_size - earliest_start;
			if (available < 0)
				return false;

			for (int pass = 0; pass < 2; ++pass) {
				const int64_t record_size = pass == 0 ? repair_rectangle_size : repair_wide_rectangle_size;
				for (int64_t k = 0; k * record_size <= available && k <= repair_max_rectangles; ++k) {
					const int64_t candidate = image_start - repair_min_sprite_entry_size - k * record_size;
					const bool count_match = i32le(&b[static_cast<size_t>(candidate + repair_sprite_header_size)]) == k;
					const bool magic = looks_like_sprite_start(b, candidate);
					const bool in_table = table_lengths.count(candidate) != 0;
					const bool contiguous = candidate == previous_end;
					const int evidence = (magic ? 1 : 0) + (in_table ? 1 : 0) + (contiguous ? 1 : 0);

					// A wide layout is only taken on a matching count. A 12-byte layout also on starting where the previous
					// sprite ended, or on two other pieces of evidence; this recovers the frame count when it is damaged.
					// A count of 0 on its own proves little (any four zero bytes match), so it needs support too.
					const bool plausible = record_size == repair_rectangle_size
						? (count_match && (k > 0 || evidence >= 1)) || contiguous || evidence >= 2
						: count_match && k > 0 && evidence >= 1;
					if (!plausible)
						continue;

					// Contiguity outweighs a bare count match: a last frame with zero pivots reads as a count of 0
					const int score = (count_match ? 3 : 0) + (magic ? 2 : 0) + (in_table ? 2 : 0) + (contiguous ? 3 : 0);
					if (score > best_score || (score == best_score && record_size == repair_rectangle_size && !wide && k > rectangle_count)) {
						best_score = score;
						start = candidate;
						rectangle_count = static_cast<int>(k);
						count_recovered = !count_match;
						wide = record_size == repair_wide_rectangle_size;
					}
				}
			}

			return start >= 0;
		}

		inline std::vector<repair_slot> find_sprites(const bytes& b, int64_t floor, int64_t to, const repair_table& table) {
			std::vector<repair_slot> slots;
			repair_lengths table_lengths;
			for (const std::pair<int64_t, int64_t>& entry : table)
				table_lengths.insert(entry);   // the first entry for an offset wins

			// Every sprite header magic in the data, in order
			std::vector<int64_t> headers;
			for (int64_t found = find_sprite_magic(b, floor, to); found >= 0; found = find_sprite_magic(b, found + 1, to))
				headers.push_back(found);

			// Each found image claims its bytes and the scan resumes after it, so the work stays linear in the file size
			int64_t previous_end = floor;
			for (int64_t position = floor + repair_min_sprite_entry_size; position < to; ++position) {
				const repair_probe img = probe_image(b, position, to);
				if (!img.is_image)
					continue;

				// The nearest sprite header in front of the image: an image inside its fixed part (header, rectangle count,
				// color key) is a false match, and the sprite cannot start before it, so frames never run over another header
				const int64_t last_header = last_before(headers, position, previous_end);
				if (last_header >= 0 && position < last_header + repair_min_sprite_entry_size)
					continue;
				const int64_t earliest_start = std::max(previous_end, last_header);

				// An incomplete image only claims up to the next sprite header, so it cannot swallow the sprites after it
				const int64_t image_claim_end = img.complete ? position + img.length : incomplete_end(b, position, to);

				int64_t start = -1;
				int rectangle_count = 0;
				bool count_recovered = false, wide = false;
				if (!try_find_entry_start(b, position, previous_end, earliest_start, table_lengths, start, rectangle_count, count_recovered, wide)) {
					repair_slot slot;
					slot.position = position;
					slot.end = image_claim_end;
					slot.problem = "Image data found at " + num(position) + " but its sprite header and frames could not be recovered";
					slots.push_back(slot);
					previous_end = image_claim_end;
					position = image_claim_end - 1;
					continue;
				}

				repair_slot slot;
				slot.position = start;
				slot.rectangle_count_hint = rectangle_count;
				if (wide) {
					slot.end = image_claim_end;
					slot.problem = "Uses " + num(repair_wide_rectangle_size) + "-byte rectangle records, which are not supported";
				}
				else if (!img.complete) {
					slot.end = image_claim_end;
					slot.problem = "Image data is incomplete";
				}
				else {
					int64_t image_end = position + img.length;

					// Keep bytes the sprite table assigns to this entry after the image, as the reader does
					const repair_lengths::const_iterator listed = table_lengths.find(start);
					if (listed != table_lengths.end() && start + listed->second > image_end && start + listed->second <= to
						&& find_sprite_magic(b, image_end, start + listed->second) < 0) {
						image_end = start + listed->second;
					}

					sprite spr;
					for (int i = 0; i < rectangle_count; ++i) {
						const uint8_t* r = &b[static_cast<size_t>(start + repair_sprite_header_size + 4 + static_cast<int64_t>(i) * repair_rectangle_size)];
						sprite_rect rect;
						rect.x = u16le(r);
						rect.y = u16le(r + 2);
						rect.width = u16le(r + 4);
						rect.height = u16le(r + 6);
						rect.pivotX = i16le(r + 8);
						rect.pivotY = i16le(r + 10);
						spr.sprite_rectangles.push_back(rect);
					}
					spr.set_color_key(u32be(&b[static_cast<size_t>(position - 4)]));
					spr.image_data.assign(b.begin() + static_cast<std::ptrdiff_t>(position), b.begin() + static_cast<std::ptrdiff_t>(image_end));
					if (img.size_field_damaged) {
						// The pixel layout fixes the length; correct the BMP size field so the reader accepts the image
						repair_note note;
						note.offset = position + 2;
						note.text = "BMP size field was " + num(u32le(&b[static_cast<size_t>(position + 2)])) + ", image is " +
							num(static_cast<long long>(spr.image_data.size())) + " bytes; rebuilt.";
						slot.notes.push_back(note);
						set_u32le(spr.image_data, 2, static_cast<uint32_t>(spr.image_data.size()));
					}

					slot.end = image_end;

					// BMPs are saved as 24bpp, so one that cannot be decoded could not be written; treat it as corrupt
					bool decode_failed = false;
					std::string decode_error;
					try {
						sprite_image::check_size(spr.image_data);
					}
					catch (const invalid_data_error& ex) {
						decode_failed = true;
						decode_error = ex.what();
					}
					if (!decode_failed && spr.image_format() == sprite_image_format::bmp && sprite_image::get_bmp_bits_per_pixel(spr.image_data) != 24) {
						try {
							decode(spr.image_data);
						}
						catch (const invalid_data_error& ex) {
							decode_failed = true;
							decode_error = ex.what();
						}
					}
					if (decode_failed) {
						slot.problem = "Image data cannot be decoded (" + decode_error + ")";
						slot.notes.clear();
						slots.push_back(slot);
						previous_end = slot.end;
						position = slot.end - 1;
						continue;
					}
					slot.has_sprite = true;
					if (!looks_like_sprite_start(b, start)) {
						repair_note note;
						note.offset = start;
						note.text = "Sprite header magic was invalid; rebuilt.";
						slot.notes.push_back(note);
					}
					if (count_recovered) {
						repair_note note;
						note.offset = start + repair_sprite_header_size;
						note.text = "Rectangle count was " + num(i32le(&b[static_cast<size_t>(start + repair_sprite_header_size)])) + ", " +
							num(rectangle_count) + " frames found; rebuilt.";
						slot.notes.push_back(note);
					}
					slot.spr = std::move(spr);
				}

				previous_end = slot.end;
				position = slot.end - 1;
				slots.push_back(std::move(slot));
			}

			return slots;
		}

		// Matches the sprite table to the sprites found, walking both in file order. An entry pointing into an unmatched
		// sprite matches it; an entry pointing at nothing usable (garbage, or into a sprite already matched) is taken to
		// be the next sprite in order; an entry pointing at an unclaimed place in the file is a lost sprite there.
		// Expects slots sorted by position.
		inline void check_repair_table(const bytes& b, const repair_table& table, int64_t table_end, std::vector<repair_slot>& slots, repair_report& report) {
			std::vector<bool> matched(slots.size(), false);
			std::vector<repair_slot> lost;
			std::set<int64_t> lost_offsets;
			size_t next = 0;

			for (size_t i = 0; i < table.size(); ++i) {
				const int64_t offset = table[i].first;
				const int64_t length = table[i].second;
				const int64_t entry = repair_sprite_table_offset + static_cast<int64_t>(i) * repair_table_entry_size;
				while (next < slots.size() && matched[next])
					++next;

				const int containing = find_containing(slots, offset);
				int slot = containing >= 0 && !matched[static_cast<size_t>(containing)] ? containing : -1;

				const bool offset_usable = containing < 0 && lost_offsets.count(offset) == 0
					&& offset >= table_end && offset + repair_min_sprite_entry_size <= static_cast<int64_t>(b.size());
				if (slot < 0 && !offset_usable && next < slots.size())
					slot = static_cast<int>(next);

				if (slot >= 0) {
					repair_slot& s = slots[static_cast<size_t>(slot)];
					matched[static_cast<size_t>(slot)] = true;
					repair_note note;
					if (s.position != offset) {
						note.offset = entry;
						note.text = "Sprite table offset was " + num(offset) + ", sprite found at " + num(s.position) + "; rebuilt.";
						s.notes.push_back(note);
					}
					else if (s.has_sprite && s.end - s.position != length) {
						note.offset = entry + 4;
						note.text = "Sprite table length was " + num(length) + ", sprite is " + num(s.end - s.position) + " bytes; rebuilt.";
						s.notes.push_back(note);
					}
					continue;
				}

				if (!offset_usable) {
					// Nothing left to match and nowhere to put it; say so rather than lose it silently
					add_issue(report, -1, entry, "Sprite table entry " + num(static_cast<long long>(i)) + " (offset " + num(offset) + ", length " +
						num(length) + ") matches no recoverable sprite.", false);
					continue;
				}

				const int32_t hint = i32le(&b[static_cast<size_t>(offset + repair_sprite_header_size)]);
				repair_slot lost_slot;
				lost_slot.position = offset;
				lost_slot.end = offset;
				lost_slot.rectangle_count_hint = hint >= 0 && hint <= repair_max_rectangles ? hint : -1;
				lost_slot.problem = "Listed in the sprite table (offset " + num(offset) + ", length " + num(length) + ") but no sprite could be recovered there";
				lost.push_back(lost_slot);
				lost_offsets.insert(offset);
			}

			if (!table.empty()) {
				for (size_t i = 0; i < slots.size(); ++i) {
					if (slots[i].has_sprite && !matched[i]) {
						repair_note note;
						note.offset = slots[i].position;
						note.text = "Sprite was not listed in the sprite table; added.";
						slots[i].notes.push_back(note);
					}
				}
			}
			slots.insert(slots.end(), lost.begin(), lost.end());
			sort_slots(slots);
		}

		// Sprite header magic with no image after it is a corrupt sprite too. Its slot covers the fixed part of the
		// entry (up to the next slot), so a sprite table offset that is slightly off still matches it.
		// Expects slots sorted by position, and keeps them sorted.
		inline void add_lost_headers(const bytes& b, int64_t floor, int64_t to, std::vector<repair_slot>& slots) {
			std::vector<repair_slot> headers;
			for (int64_t found = find_sprite_magic(b, floor, to); found >= 0; found = find_sprite_magic(b, found + 1, to)) {
				if (find_containing(slots, found) >= 0 || (!headers.empty() && found < headers.back().end))
					continue;

				const int32_t hint = found + repair_min_sprite_entry_size <= static_cast<int64_t>(b.size())
					? i32le(&b[static_cast<size_t>(found + repair_sprite_header_size)]) : -1;
				int64_t end = std::min(found + repair_min_sprite_entry_size, to);
				for (const repair_slot& s : slots) {
					if (s.position > found) {
						end = std::min(end, s.position);
						break;
					}
				}
				repair_slot slot;
				slot.position = found;
				slot.end = end;
				slot.rectangle_count_hint = hint >= 0 && hint <= repair_max_rectangles ? hint : -1;
				slot.problem = "Sprite header found but its image data is missing or damaged";
				headers.push_back(slot);
			}

			slots.insert(slots.end(), headers.begin(), headers.end());
			sort_slots(slots);
		}

		// 32x32 24-bit BMP, an 8-pixel checkerboard of magenta (the usual transparency key) and black
		inline sprite create_placeholder(int rectangle_count) {
			const size_t size = static_cast<size_t>(repair_placeholder_size);
			const size_t row_size = size * 3;
			bytes bmp(54 + row_size * size, 0);
			bmp[0] = 0x42;
			bmp[1] = 0x4D;
			set_u32le(bmp, 2, static_cast<uint32_t>(bmp.size()));
			set_u32le(bmp, 10, 54);
			set_u32le(bmp, 14, 40);
			set_u32le(bmp, 18, static_cast<uint32_t>(size));
			set_u32le(bmp, 22, static_cast<uint32_t>(size));
			set_u16le(bmp, 26, 1);
			set_u16le(bmp, 28, 24);
			for (size_t y = 0; y < size; ++y) {
				for (size_t x = 0; x < size; ++x) {
					if (((x / 8) + (y / 8)) % 2 != 0)
						continue;
					const size_t pixel = 54 + y * row_size + x * 3;
					bmp[pixel] = 0xFF;       // B
					bmp[pixel + 2] = 0xFF;   // R
				}
			}

			// Keep the frame count so frame indices stay valid; every frame shows the whole placeholder
			sprite s;
			s.image_data = bmp;
			for (int i = 0; i < std::max(rectangle_count, 1); ++i) {
				sprite_rect rect{};
				rect.width = static_cast<uint16_t>(repair_placeholder_size);
				rect.height = static_cast<uint16_t>(repair_placeholder_size);
				s.sprite_rectangles.push_back(rect);
			}
			return s;
		}

		inline repair_report repair(const bytes& b, int64_t from, int64_t to, bool force, bool whole_file) {
			repair_report report;

			int64_t table_end = 0;
			bool table_readable = false;
			const repair_table table = read_repair_table(b, whole_file ? &report : nullptr, table_end, table_readable);
			const int64_t floor = whole_file ? table_end : from;

			// Slots come out of each step sorted by position
			std::vector<repair_slot> slots = find_sprites(b, floor, to, table);
			add_lost_headers(b, floor, to, slots);
			if (whole_file) {
				check_repair_table(b, table, table_end, slots, report);
				if (table_readable && table.empty() && !slots.empty())
					add_issue(report, -1, repair_sprite_count_offset, "Sprite count was 0 but " + num(static_cast<long long>(slots.size())) +
						" sprites were found; table rebuilt.", true);
			}

			if (whole_file && slots.empty() && !b.empty())
				add_issue(report, -1, 0, "No sprites could be found.", false);

			int index = 0;
			for (repair_slot& slot : slots) {
				for (const repair_note& note : slot.notes)
					add_issue(report, index, note.offset, note.text, true);

				if (slot.has_sprite) {
					report.data.sprites.push_back(slot.spr);
				}
				else if (force) {
					report.data.sprites.push_back(create_placeholder(slot.rectangle_count_hint));
					report.placeholder_sprites.push_back(static_cast<int>(report.data.sprites.size()) - 1);
					add_issue(report, index, slot.position, slot.problem + "; replaced with a " + num(repair_placeholder_size) + "x" +
						num(repair_placeholder_size) + " placeholder.", true);
				}
				else {
					add_issue(report, index, slot.position, slot.problem + "; dropped (repair with force to insert a placeholder).", false);
				}
				++index;
			}

			if (whole_file && !slots.empty()) {
				int64_t last_end = slots.front().end;
				for (const repair_slot& slot : slots)
					last_end = std::max(last_end, slot.end);
				if (last_end < static_cast<int64_t>(b.size()))
					add_issue(report, -1, last_end, num(static_cast<int64_t>(b.size()) - last_end) + " trailing bytes after the last sprite; dropped.", true);
			}

			std::memcpy(report.data.header.signature, detail::magic::file, file_header_size);
			report.data.sprite_count = static_cast<uint32_t>(report.data.sprites.size());
			return report;
		}
	}

	namespace pak_repair {
		// PAKRepair.Repair(bytes, force): salvages every sprite in a PAK file held in memory
		inline repair_report repair(const std::vector<uint8_t>& bytes, bool force = false) {
			return detail::repair(bytes, 0, static_cast<int64_t>(bytes.size()), force, true);
		}

		// PAKRepair.Repair(bytes, start, end, force): salvages the sprites in one section [start, end) of a PAK file.
		// The section must contain whole sprite entries.
		inline repair_report repair(const std::vector<uint8_t>& bytes, int64_t start, int64_t end, bool force = false) {
			if (start < 0 || end > static_cast<int64_t>(bytes.size()) || start > end)
				throw argument_out_of_range_error("Section must lie within the data.");
			return detail::repair(bytes, start, end, force, false);
		}
	}

	// PAK.RepairFile: salvages the sprites of a damaged PAK file and writes a rebuilt file to output_filepath.
	// Corrupt sprites are dropped, or replaced with a 32x32 placeholder when force is true; check the report.
	// Both paths may be relative or absolute; the output must differ from the input (full paths, ignoring case).
	inline repair_report repair_file(const std::string& filepath, const std::string& output_filepath, bool force = false) {
		if (detail::is_null_or_white_space(filepath))
			throw std::invalid_argument("File path cannot be null or empty.");
		if (!detail::file_exists(filepath))
			throw file_not_found_error("The specified file does not exist.", filepath);
		if (detail::is_null_or_white_space(output_filepath))
			throw std::invalid_argument("Output file path cannot be null or empty.");
		if (detail::same_full_path(filepath, output_filepath))
			throw std::invalid_argument("Output file path must differ from the input file path.");

		repair_report report = pak_repair::repair(detail::read_all_bytes(filepath), force);
		report.data.write(output_filepath);
		return report;
	}

	// ---------------------------------------------------------------------------------------------
	// Lenient readers for the game (no PAKLib counterpart): no magic or image checks
	// ---------------------------------------------------------------------------------------------
	// The same count and bounds rules as loadpak / get_sprite (invalid_data_error before anything is allocated),
	// without the magic, BMP / PNG and size checks. No path checks; the path is opened exactly as given.
	inline pak loadpak_fast(const std::string& filepath) {
		std::ifstream file(filepath, std::ios::binary);
		if (!file.is_open())
			throw std::runtime_error("Failed to open PAK file: " + filepath);
		file.seekg(0, std::ios::end);
		const detail::file_source src{ file, static_cast<int64_t>(file.tellg()), filepath };

		pak result{};
		detail::read_header_and_table(src, result, false);
		result.sprites.resize(result.sprite_entries.size());
		for (size_t i = 0; i < result.sprite_entries.size(); ++i) {
			const sprite_entry& entry = result.sprite_entries[i];
			detail::read_sprite_entry(src, entry.offset, entry.offset, entry.size, i, result.sprites[i], false);
		}
		return result;
	}

	inline sprite get_sprite_fast(const std::string& filepath, size_t sprite_index) {
		std::ifstream file(filepath, std::ios::binary);
		if (!file.is_open())
			throw std::runtime_error("Failed to open PAK file: " + filepath);
		file.seekg(0, std::ios::end);
		const detail::file_source src{ file, static_cast<int64_t>(file.tellg()), filepath };

		pak table{};
		detail::read_header_and_table(src, table, false);
		if (sprite_index >= table.sprite_entries.size())
			throw argument_out_of_range_error("Sprite index out of bounds in PAK file: " + filepath);

		const sprite_entry& entry = table.sprite_entries[sprite_index];
		sprite result;
		detail::read_sprite_entry(src, entry.offset, entry.offset, entry.size, sprite_index, result, false);
		return result;
	}
}
