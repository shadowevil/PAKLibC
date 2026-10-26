// Scenarios for the per-sprite transparency colour key: bytes A RR GG BB, A == FF means set,
// any other alpha is normalised to 00 00 00 00 (unset)
#include "PakFixture.h"

TEST_CASE("Colour key API: a new sprite has no key") {
	const PAKLib::sprite s{};
	CHECK(!s.has_color_key());
	CHECK(s.get_color_key() == 0);
}

TEST_CASE("Colour key API: set_color_key(r, g, b) sets alpha FF") {
	PAKLib::sprite s{};
	s.set_color_key(0x28, 0x28, 0x31);
	CHECK(s.has_color_key());
	CHECK(s.get_color_key() == 0xFF282831u);
	CHECK(s.color_key_r() == 0x28);
	CHECK(s.color_key_g() == 0x28);
	CHECK(s.color_key_b() == 0x31);
}

TEST_CASE("Colour key API: black is a real key, distinct from unset") {
	PAKLib::sprite s{};
	s.set_color_key(0, 0, 0);
	CHECK(s.has_color_key());
	CHECK(s.get_color_key() == 0xFF000000u);
}

TEST_CASE("Colour key API: any alpha other than FF drops the whole value") {
	const uint32_t not_set[] = { 0x00282831u, 0x7F112233u, 0xFE000000u, 0x01FFFFFFu, 0x00000000u };
	for (uint32_t value : not_set) {
		PAKLib::sprite s{};
		s.set_color_key(0xFF123456u);
		s.set_color_key(value);
		CHECK(!s.has_color_key());
		CHECK(s.get_color_key() == 0);
	}
}

TEST_CASE("Colour key API: clear_color_key unsets") {
	PAKLib::sprite s{};
	s.set_color_key(1, 2, 3);
	s.clear_color_key();
	CHECK(!s.has_color_key());
	CHECK(s.get_color_key() == 0);
}

TEST_CASE("Colour key on disk: every loader reads and normalises the 4 bytes") {
	struct key_case { uint8_t bytes[4]; uint32_t expected; };
	const key_case cases[] = {
		{ { 0xFF, 0x28, 0x28, 0x31 }, 0xFF282831u }, // authored key
		{ { 0xFF, 0x00, 0x00, 0x00 }, 0xFF000000u }, // black
		{ { 0x00, 0x28, 0x28, 0x31 }, 0u },          // original client layout: unset, RGB dropped
		{ { 0x7F, 0x11, 0x22, 0x33 }, 0u },          // stray alpha: unset
		{ { 0x00, 0x00, 0x00, 0x00 }, 0u },          // unset
		{ { 0xFF, 0xFF, 0xFF, 0xFF }, 0xFFFFFFFFu }, // white
	};
	const size_t count = sizeof(cases) / sizeof(cases[0]);
	std::vector<fixture::sprite_spec> specs(count);
	for (size_t i = 0; i < count; ++i)
		std::memcpy(specs[i].color_key, cases[i].bytes, 4);
	const std::string path = fixture::write("color_keys.pak", fixture::build_pak(specs).data);

	for (const fixture::full_loader& loader : fixture::full_loaders()) {
		const PAKLib::pak pak_file = loader.load(path);
		REQUIRE(pak_file.sprites.size() == count);
		for (size_t i = 0; i < count; ++i)
			CHECK(pak_file.sprites[i].get_color_key() == cases[i].expected);
	}
	for (const fixture::sprite_loader& loader : fixture::sprite_loaders())
		for (size_t i = 0; i < count; ++i)
			CHECK(loader.load(path, i).get_color_key() == cases[i].expected);
}

TEST_CASE("Colour key on disk: reading the key does not shift the image bytes") {
	std::vector<fixture::sprite_spec> specs(2);
	const uint8_t key[4] = { 0xFF, 0xAB, 0xCD, 0xEF };
	std::memcpy(specs[0].color_key, key, 4);
	specs[0].image = fixture::make_bmp(2, 2, 0xAB, 0xCD, 0xEF);
	specs[1].image = fixture::make_png_header(4, 4);
	const std::string path = fixture::write("color_key_image.pak", fixture::build_pak(specs).data);
	for (const fixture::full_loader& loader : fixture::full_loaders()) {
		const PAKLib::pak pak_file = loader.load(path);
		REQUIRE(pak_file.sprites.size() == 2);
		CHECK(pak_file.sprites[0].image_data == specs[0].image);
		CHECK(pak_file.sprites[1].image_data == specs[1].image);
	}
	for (const fixture::sprite_loader& loader : fixture::sprite_loaders()) {
		CHECK(loader.load(path, 0).image_data == specs[0].image);
		CHECK(loader.load(path, 1).image_data == specs[1].image);
	}
}
