#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <iterator>
#include <stdexcept>
#include <fstream>
#include <type_traits>

namespace PAKLib {
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

	struct sprite {
	public:
		sprite_header header{};
		std::vector<sprite_rect> sprite_rectangles{};
		std::vector<uint8_t> image_data; // Raw image data (e.g., PNG, JPEG)

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

	private:
		uint32_t color_key{ 0 };
	};

	struct sprite_entry {
		uint32_t offset{ 0 };
		uint32_t size{ 0 };
	};

	struct pak {
	public:
		std::string pak_file_path{};
		file_header header{};
		uint32_t sprite_count{ 0 };
		std::vector<sprite_entry> sprite_entries;
		std::vector<sprite> sprites{};
	};

	constexpr size_t SPRITE_HEADER_SKIP = sizeof(sprite_header);
	constexpr size_t RECT_COUNT_SIZE = sizeof(uint32_t);
	constexpr size_t COLOR_KEY_SIZE = sizeof(uint32_t);
#pragma pack(pop)

	// Transparency color key, stored as bytes A RR GG BB; returned as 0xAARRGGBB
	inline uint32_t read_color_key(std::istream& stream) {
		uint8_t bytes[4]{ 0 };
		stream.read(reinterpret_cast<char*>(bytes), sizeof(bytes));
		return (static_cast<uint32_t>(bytes[0]) << 24) | (static_cast<uint32_t>(bytes[1]) << 16) |
			(static_cast<uint32_t>(bytes[2]) << 8) | static_cast<uint32_t>(bytes[3]);
	}

	inline static pak loadpak(const std::string& filepath) {
		auto pak_file = pak{};
		std::ifstream file(filepath, std::ios::binary);
		if (!file.is_open()) {
			throw std::runtime_error("Failed to open PAK file: " + filepath);
		}

		// Read file header
		file.read(reinterpret_cast<char*>(&pak_file.header), sizeof(file_header));
		if (std::string(pak_file.header.signature, file_header_size) != file_header_signature) {
			throw std::runtime_error("Invalid PAK file header in file: " + filepath);
		}

		// Read sprite count
		file.read(reinterpret_cast<char*>(&pak_file.sprite_count), sizeof(uint32_t));
		if (pak_file.sprite_count == 0) {
			throw std::runtime_error("No sprites found in PAK file: " + filepath);
		}
		if (pak_file.sprite_count > 1500) {
			throw std::runtime_error("Unreasonable number of sprites detected, overflow?: " + filepath);
		}

		// Read sprite entries
		pak_file.sprite_entries.resize(pak_file.sprite_count);
		file.read(reinterpret_cast<char*>(pak_file.sprite_entries.data()), pak_file.sprite_count * sizeof(sprite_entry));
		if (file.fail()) {
			throw std::runtime_error("Failed to read sprite entries from PAK file: " + filepath);
		}
		if (pak_file.sprite_entries.size() != pak_file.sprite_count) {
			throw std::runtime_error("Mismatch in sprite entries count in PAK file: " + filepath);
		}

		// Read each sprite
		pak_file.sprites.resize(pak_file.sprite_count);
		for (auto it = pak_file.sprite_entries.begin(); it != pak_file.sprite_entries.end(); it++) {
			auto& entry = *it;
			size_t index = std::distance(pak_file.sprite_entries.begin(), it);
			auto& offset = entry.offset;
			auto& size = entry.size;

			if (file.tellg() != static_cast<std::streampos>(offset)) {
				throw std::runtime_error("Sprite offset mismatch in PAK file, corrupt PAK file?: " + filepath);
			}

			auto& sprite_obj = pak_file.sprites[index];

			// Read sprite header
			file.read(reinterpret_cast<char*>(&sprite_obj.header), sizeof(sprite_header));
			if (std::string(sprite_obj.header.signature, sprite_header_size) != sprite_header_signature) {
				throw std::runtime_error("Invalid sprite header in PAK file: " + filepath);
			}

			// Read sprite rectangles count
			uint32_t rect_count = 0;
			file.read(reinterpret_cast<char*>(&rect_count), sizeof(uint32_t));
			if (rect_count == 0 || rect_count > 1000) {
				throw std::runtime_error("Unreasonable number of sprite rectangles detected, overflow?: " + filepath);
			}

			// Read sprite rectangles
			sprite_obj.sprite_rectangles.resize(rect_count);
			file.read(reinterpret_cast<char*>(sprite_obj.sprite_rectangles.data()), rect_count * sizeof(sprite_rect));
			if (file.fail()) {
				throw std::runtime_error("Failed to read sprite rectangles from PAK file: " + filepath);
			}
			if (sprite_obj.sprite_rectangles.size() != rect_count) {
				throw std::runtime_error("Mismatch in sprite rectangles count in PAK file: " + filepath);
			}

			// Transparency color key, stored as bytes A RR GG BB (alpha other than FF = none authored)
			sprite_obj.set_color_key(read_color_key(file));

			// Read image data
			int sizeOfSpriteHeader = sizeof(sprite_header);
			int sizeOfRectangleCount = sizeof(uint32_t);
			int sizeOfSpriteRectangles = (rect_count * sizeof(sprite_rect)) + sizeOfRectangleCount;
			int sizeOfColorKey = sizeof(uint32_t);
			long newLength = size - (sizeOfSpriteHeader + sizeOfSpriteRectangles) - sizeOfColorKey;
			sprite_obj.image_data.resize(newLength);
			file.read(reinterpret_cast<char*>(sprite_obj.image_data.data()), newLength);
			if (file.fail()) {
				throw std::runtime_error("Failed to read image data from PAK file: " + filepath);
			}
		}

		pak_file.pak_file_path = filepath;
		return pak_file;
	}

	inline static pak loadpak_fast(const std::string& filepath) {
		pak pak_file{};
		std::ifstream file(filepath, std::ios::binary);
		if (!file.is_open()) {
			throw std::runtime_error("Failed to open PAK file: " + filepath);
		}

		file.seekg(sizeof(file_header), std::ios::beg);

		file.read(reinterpret_cast<char*>(&pak_file.sprite_count), sizeof(uint32_t));
		if (pak_file.sprite_count == 0 || pak_file.sprite_count > 1500) {
			throw std::runtime_error("Invalid sprite count in: " + filepath);
		}

		pak_file.sprite_entries.resize(pak_file.sprite_count);
		file.read(reinterpret_cast<char*>(pak_file.sprite_entries.data()), pak_file.sprite_count * sizeof(sprite_entry));
		if (file.fail()) {
			throw std::runtime_error("Failed to read sprite entries from: " + filepath);
		}

		pak_file.sprites.resize(pak_file.sprite_count);
		for (size_t i = 0; i < pak_file.sprite_count; ++i) {
			const auto& entry = pak_file.sprite_entries[i];
			file.seekg(entry.offset + SPRITE_HEADER_SKIP, std::ios::beg);

			uint32_t rect_count = 0;
			file.read(reinterpret_cast<char*>(&rect_count), sizeof(uint32_t));
			if (rect_count == 0 || rect_count > 1000) {
				throw std::runtime_error("Invalid rect count in: " + filepath);
			}

			auto& sprite_obj = pak_file.sprites[i];
			sprite_obj.sprite_rectangles.resize(rect_count);
			file.read(reinterpret_cast<char*>(sprite_obj.sprite_rectangles.data()), rect_count * sizeof(sprite_rect));
			if (file.fail()) {
				throw std::runtime_error("Failed to read sprite rectangles from: " + filepath);
			}

			// Transparency color key, stored as bytes A RR GG BB (alpha other than FF = none authored)
			sprite_obj.set_color_key(read_color_key(file));

			const long image_byte_size =
				entry.size - (SPRITE_HEADER_SKIP + (rect_count * sizeof(sprite_rect)) + RECT_COUNT_SIZE + COLOR_KEY_SIZE);

			sprite_obj.image_data.resize(image_byte_size);
			file.read(reinterpret_cast<char*>(sprite_obj.image_data.data()), image_byte_size);
			if (file.fail()) {
				throw std::runtime_error("Failed to read image data from: " + filepath);
			}
		}

		pak_file.pak_file_path = filepath;
		return pak_file;
	}

	inline static sprite get_sprite(const std::string& filepath, size_t sprite_index) {
		std::ifstream file(filepath, std::ios::binary);
		if (!file.is_open()) {
			throw std::runtime_error("Failed to open PAK file: " + filepath);
		}

		// Read file header
		auto pak_file_header = file_header{};
		file.read(reinterpret_cast<char*>(&pak_file_header), sizeof(file_header));
		if (std::string(pak_file_header.signature, file_header_size) != file_header_signature) {
			throw std::runtime_error("Invalid PAK file header in file: " + filepath);
		}

		// Read sprite count
		uint32_t sprite_count = 0;
		file.read(reinterpret_cast<char*>(&sprite_count), sizeof(uint32_t));
		if (sprite_count == 0) {
			throw std::runtime_error("No sprites found in PAK file: " + filepath);
		}
		if (sprite_count > 1500) {
			throw std::runtime_error("Unreasonable number of sprites detected, overflow?: " + filepath);
		}
		if (sprite_index >= sprite_count) {
			throw std::runtime_error("Sprite index out of bounds in PAK file: " + filepath);
		}

		// Read sprite entries
		std::vector<sprite_entry> sprite_entries{};
		sprite_entries.resize(sprite_count);
		file.read(reinterpret_cast<char*>(sprite_entries.data()), sprite_count * sizeof(sprite_entry));
		if (file.fail()) {
			throw std::runtime_error("Failed to read sprite entries from PAK file: " + filepath);
		}
		if (sprite_entries.size() != sprite_count) {
			throw std::runtime_error("Mismatch in sprite entries count in PAK file: " + filepath);
		}

		// Seek to the desired sprite
		auto& entry = sprite_entries[sprite_index];
		file.seekg(entry.offset, std::ios::beg);
		auto sprite_obj = sprite{};

		// Read sprite header
		file.read(reinterpret_cast<char*>(&sprite_obj.header), sizeof(sprite_header));
		if (std::string(sprite_obj.header.signature, sprite_header_size) != sprite_header_signature) {
			throw std::runtime_error("Invalid sprite header in PAK file: " + filepath);
		}

		// Read sprite rectangles count
		uint32_t rect_count = 0;
		file.read(reinterpret_cast<char*>(&rect_count), sizeof(uint32_t));
		if (rect_count == 0 || rect_count > 1000) {
			throw std::runtime_error("Unreasonable number of sprite rectangles detected, overflow?: " + filepath);
		}

		// Read sprite rectangles
		sprite_obj.sprite_rectangles.resize(rect_count);
		file.read(reinterpret_cast<char*>(sprite_obj.sprite_rectangles.data()), rect_count * sizeof(sprite_rect));
		if (file.fail()) {
			throw std::runtime_error("Failed to read sprite rectangles from PAK file: " + filepath);
		}
		if (sprite_obj.sprite_rectangles.size() != rect_count) {
			throw std::runtime_error("Mismatch in sprite rectangles count in PAK file: " + filepath);
		}

		// Transparency color key, stored as bytes A RR GG BB (alpha other than FF = none authored)
		sprite_obj.set_color_key(read_color_key(file));

		// Read image data
		long image_byte_size = entry.size - (sizeof(sprite_header) + ((rect_count * sizeof(sprite_rect)) + sizeof(uint32_t))) - COLOR_KEY_SIZE;
		sprite_obj.image_data.resize(image_byte_size);
		file.read(reinterpret_cast<char*>(sprite_obj.image_data.data()), image_byte_size);
		if (file.fail()) {
			throw std::runtime_error("Failed to read image data from PAK file: " + filepath);
		}

		return sprite_obj;
	}

	inline static sprite get_sprite_fast(const std::string& filepath, size_t sprite_index) {
		std::ifstream file(filepath, std::ios::binary);
		if (!file.is_open()) {
			throw std::runtime_error("Failed to open PAK file: " + filepath);
		}

		sprite_entry entry{};
		file.seekg(sizeof(file_header) + sizeof(uint32_t) + (sprite_index * sizeof(sprite_entry)), std::ios::beg);
		file.read(reinterpret_cast<char*>(&entry), sizeof(entry));
		if (file.fail()) {
			throw std::runtime_error("Failed to read sprite entry from: " + filepath);
		}

		file.seekg(entry.offset + SPRITE_HEADER_SKIP, std::ios::beg);

		sprite sprite_obj{};
		uint32_t rect_count = 0;
		file.read(reinterpret_cast<char*>(&rect_count), sizeof(uint32_t));
		if (rect_count == 0 || rect_count > 1000) {
			throw std::runtime_error("Invalid rect count in: " + filepath);
		}

		sprite_obj.sprite_rectangles.resize(rect_count);
		file.read(reinterpret_cast<char*>(sprite_obj.sprite_rectangles.data()), rect_count * sizeof(sprite_rect));
		if (file.fail()) {
			throw std::runtime_error("Failed to read sprite rectangles from: " + filepath);
		}

		// Transparency color key, stored as bytes A RR GG BB (alpha other than FF = none authored)
		sprite_obj.set_color_key(read_color_key(file));

		const long image_byte_size =
			entry.size - (SPRITE_HEADER_SKIP + (rect_count * sizeof(sprite_rect)) + RECT_COUNT_SIZE + COLOR_KEY_SIZE);

		sprite_obj.image_data.resize(image_byte_size);
		file.read(reinterpret_cast<char*>(sprite_obj.image_data.data()), image_byte_size);
		if (file.fail()) {
			throw std::runtime_error("Failed to read image data from: " + filepath);
		}

		return sprite_obj;
	}
}