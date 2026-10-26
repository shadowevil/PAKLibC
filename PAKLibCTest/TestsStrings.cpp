// Non-ASCII literals are \x code units so the source stays ASCII.
// .NET string semantics: char.IsWhiteSpace, StringComparison.OrdinalIgnoreCase, Path.GetExtension(...).ToLower()
// and Path.GetFullPath as used by PAK.ReadFromFile / PAK.RepairFile. Expected results are .NET 8's, from the casing
// probe (see Parity\README.md).
// Paths are judged in the encoding the file functions decode them with: the ANSI code page on Windows, UTF-8
// elsewhere. Path tests therefore build their names with native(), which spells a name in that encoding and
// skips (with a note) the names the current code page cannot spell.
#include "PakFixture.h"
#include <cstdio>

namespace {
	using PAKLib::detail::equals_ignore_case;
	using PAKLib::detail::is_null_or_white_space;
	using PAKLib::detail::is_white_space;
	using PAKLib::detail::lower_extension;

	const std::string must_differ = "Output file path must differ from the input file path.";

	std::string replace_all(std::string s, char from, char to) {
		for (char& c : s)
			if (c == from)
				c = to;
		return s;
	}

	std::string ascii_upper(std::string s) {
		for (char& c : s)
			if (c >= 'a' && c <= 'z')
				c = static_cast<char>(c - 'a' + 'A');
		return s;
	}

	std::string repair_input(const std::string& name) {
		return fixture::write(name, fixture::build_pak(fixture::sprites(1)).data);
	}

	// The name in the narrow encoding the file functions use, or "" when that encoding cannot spell it
	std::string native(const std::wstring& name) {
#if defined(_WIN32)
		static const _locale_t acp = _create_locale(LC_ALL, ".ACP");
		if (acp == nullptr)
			return std::string();
		std::vector<char> buffer(name.size() * 4 + 1);
		size_t converted = 0;
		if (_wcstombs_s_l(&converted, buffer.data(), buffer.size(), name.c_str(), _TRUNCATE, acp) != 0)
			return std::string();
		const std::string narrow(buffer.data());
		// Reject best-fit substitutions: the name must decode back to itself
		std::vector<wchar_t> back(narrow.size() + 1);
		if (_mbstowcs_s_l(&converted, back.data(), back.size(), narrow.c_str(), _TRUNCATE, acp) != 0 || std::wstring(back.data()) != name)
			return std::string();
		return narrow;
#else
		std::string out;
		for (wchar_t wc : name) {
			const uint32_t c = static_cast<uint32_t>(wc);
			if (c < 0x80) {
				out += static_cast<char>(c);
			}
			else if (c < 0x800) {
				out += static_cast<char>(0xC0 | (c >> 6));
				out += static_cast<char>(0x80 | (c & 0x3F));
			}
			else if (c < 0x10000) {
				out += static_cast<char>(0xE0 | (c >> 12));
				out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
				out += static_cast<char>(0x80 | (c & 0x3F));
			}
			else {
				out += static_cast<char>(0xF0 | (c >> 18));
				out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
				out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
				out += static_cast<char>(0x80 | (c & 0x3F));
			}
		}
		return out;
#endif
	}

	bool spelled(const std::string& name, const char* what) {
		if (!name.empty())
			return true;
		std::printf("    note: %s cannot be spelled in this code page; skipped\n", what);
		return false;
	}

	// repair_file(in, out) is refused as the same file, and the input is left as it was
	void check_refused(const std::wstring& in_name, const std::wstring& out_name, const char* what) {
		const std::string in = native(in_name), out = native(out_name);
		if (!spelled(in, what) || !spelled(out, what))
			return;
		const std::string input = repair_input(in);
		const fixture::bytes before = fixture::read(input);
		CHECK_THROWS_AS(PAKLib::repair_file(input, fixture::output_dir() + "/" + out), std::invalid_argument, must_differ);
		CHECK(fixture::read(input) == before);
	}

	// repair_file(in, out) treats them as different files and writes the output
	void check_allowed(const std::wstring& in_name, const std::wstring& out_name, const char* what) {
		const std::string in = native(in_name), out = native(out_name);
		if (!spelled(in, what) || !spelled(out, what))
			return;
		const std::string input = repair_input(in);
		const fixture::bytes before = fixture::read(input);
		CHECK(PAKLib::repair_file(input, fixture::output_dir() + "/" + out).success());
		CHECK(fixture::read(input) == before);
	}
}

TEST_CASE("Strings: char.IsWhiteSpace code points") {
	for (char32_t c : { 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x20, 0x85, 0xA0, 0x1680, 0x2000, 0x2007, 0x200A, 0x2028, 0x2029, 0x202F, 0x205F, 0x3000 })
		CHECK(is_white_space(c));
	for (char32_t c : { 0x08, 0x0E, 0x21, 0x84, 0x86, 0x180E, 0x200B, 0x200C, 0x2060, 0xFEFF, 0x110020, 0x1100A0 })
		CHECK(!is_white_space(c));                      // 0x110000 + byte: a byte that does not decode
}

TEST_CASE("Paths: IsNullOrWhiteSpace on the path as the file functions decode it") {
	CHECK(is_null_or_white_space(""));
	CHECK(is_null_or_white_space(" \t\r\n\v\f"));
	CHECK(!is_null_or_white_space(" x "));
	const std::wstring blanks[] = { L"\x00A0", L"\x3000\x2007", L"\x2003", L"\x00A0 \x00A0" };
	for (const std::wstring& blank : blanks) {
		const std::string path = native(blank);
		if (!spelled(path, "a blank name"))
			continue;
		CHECK(is_null_or_white_space(path));
		CHECK_THROWS_AS(PAKLib::loadpak(path), std::invalid_argument, "File path cannot be null or empty.");
		CHECK_THROWS_AS(PAKLib::get_sprite(path, 0), std::invalid_argument, "File path cannot be null or empty.");
		CHECK_THROWS_AS(PAKLib::save_to_file(PAKLib::pak{}, path), std::invalid_argument, "File path cannot be null or empty.");
	}
	const std::string zero_width = native(L"\x200B");   // ZERO WIDTH SPACE: not white space in .NET
	if (spelled(zero_width, "U+200B"))
		CHECK(!is_null_or_white_space(zero_width));
#if defined(_WIN32)
	// UTF-8 bytes are not UTF-8 to the file functions: C2 A0 is two characters, the first not blank
	CHECK(!is_null_or_white_space("\xC2\xA0"));
#else
	CHECK(is_null_or_white_space("\xC2\xA0"));          // U+00A0 NO-BREAK SPACE
	CHECK(!is_null_or_white_space("\xA0"));             // a stray byte
	CHECK(!is_null_or_white_space("\xC0\xA0"));         // overlong encoding of a space
#endif
}

TEST_CASE("Strings: OrdinalIgnoreCase upper-cases per character as .NET 8 does (UTF-8 text)") {
	CHECK(equals_ignore_case("caf\xC3\xA9", "CAF\xC3\x89"));                                    // cafe / CAFE with acute
	CHECK(equals_ignore_case("\xCE\xB1\xCE\xB2\xCE\xB3", "\xCE\x91\xCE\x92\xCE\x93"));          // Greek alpha beta gamma
	CHECK(equals_ignore_case("\xD0\xB4\xD0\xBE\xD0\xBC", "\xD0\x94\xD0\x9E\xD0\x9C"));          // Cyrillic dom / DOM
	CHECK(equals_ignore_case("\xCF\x82", "\xCE\xA3"));                                          // final sigma / SIGMA
	CHECK(equals_ignore_case("\xC2\xB5", "\xCE\x9C"));                                          // MICRO SIGN / MU
	CHECK(equals_ignore_case("\xE1\xBE\x80", "\xE1\xBE\x88"));                                  // U+1F80 / U+1F88 (simple mapping)
	CHECK(equals_ignore_case("\xC7\x86", "\xC7\x85"));                                          // dz caron: lower and title share U+01C4
	CHECK(equals_ignore_case("\xF0\x90\x90\xA8", "\xF0\x90\x90\x80"));                          // Deseret U+10428 / U+10400 (surrogate pair)
	CHECK(!equals_ignore_case("stra\xC3\x9F" "e", "STRASSE"));                                  // no full case folding
	CHECK(!equals_ignore_case("\xC4\xB1", "I"));                                                // U+0131 dotless i is not folded
	CHECK(!equals_ignore_case("i", "\xC4\xB0"));                                                // U+0130 capital I with dot
	CHECK(!equals_ignore_case("\xC5\xBF", "S"));                                                // U+017F long s is not folded
	CHECK(!equals_ignore_case("caf\xC3\xA9", "CAFE"));
	CHECK(!equals_ignore_case("\xC3", "\xC3\x89"));                                             // stray byte vs a character
	CHECK(equals_ignore_case("\xC3", "\xC3"));                                                  // the same stray byte
	CHECK(equals_ignore_case("ABC", "abc"));
	CHECK(!equals_ignore_case("abc", "abcd"));
	// UTF-16 input: a surrogate pair folds as one character, a lone surrogate only matches itself
	using PAKLib::detail::utf16_code_points;
	CHECK(equals_ignore_case(utf16_code_points(L"\xD801\xDC28"), utf16_code_points(L"\xD801\xDC00")));
	CHECK(equals_ignore_case(utf16_code_points(L"\xD801"), utf16_code_points(L"\xD801")));
	CHECK(!equals_ignore_case(utf16_code_points(L"\xD801"), utf16_code_points(L"\xDC00")));
}

TEST_CASE("Strings: the .epak check lowers as .NET's ToLower does") {
	const std::string message = "Encrypted .epak files are no longer supported (removed in PAKLib 2.0).";
	// On code points: U+212A KELVIN SIGN lowers to 'k', U+0130 to 'i'; a Greek kappa is just another letter
	CHECK(lower_extension(std::u32string(U"kelvin.EPA\x212A")) == U".epak");
	CHECK(lower_extension(std::u32string(U"x.\x0130")) == U".i");
	CHECK(lower_extension(std::u32string(U"greek.epa\x03BA")) == U".epa\x03BA");
	CHECK(lower_extension("dir.epak/name.PNG") == U".png");
	CHECK(lower_extension("dir.epak\\name") == U"");
	CHECK(lower_extension("name.") == U"");
	// On files
	CHECK_THROWS_AS(PAKLib::loadpak(repair_input("upper.EPAK")), PAKLib::not_supported_error, message);
	const std::string kelvin = native(L"kelvin.EPA\x212A");
	if (spelled(kelvin, "U+212A"))
		CHECK_THROWS_AS(PAKLib::loadpak(repair_input(kelvin)), PAKLib::not_supported_error, message);
	const std::string greek_k = native(L"greek.epa\x03BA");
	if (spelled(greek_k, "U+03BA"))
		CHECK(PAKLib::loadpak(repair_input(greek_k)).sprites.size() == 1);
}

TEST_CASE("Paths: repair_file refuses names the file system opens as one file") {
	check_refused(L"caf\x00E9.pak", L"CAF\x00C9.pak", "cafe with acute");                     // cp1252: E9 / C9
	check_refused(L"\x00E5ngstr\x00F6m.pak", L"\x00C5NGSTR\x00D6M.PAK", "a with ring, o with diaeresis");
	check_refused(L"\x03B1\x03B2\x03B3.pak", L"\x0391\x0392\x0393.PAK", "Greek");
	check_refused(L"\x0434\x043E\x043C.pak", L"\x0414\x041E\x041C.pak", "Cyrillic");
	// Pairs .NET keeps apart are different files: the repair runs and writes the output
	check_allowed(L"FILE_I.pak", L"FILE_\x0131.pak", "U+0131");
	check_allowed(L"STRASSE.pak", L"stra\x00DF" L"e.pak", "sharp s");
	const std::string dir = fixture::output_dir() + "/";
#if defined(_WIN32)
	// UTF-8 bytes of cafe / CAFE with acute are "caf" A9 / "CAF" 89 to the ANSI code page: two different files
	const std::string utf8_cafe = repair_input("caf\xC3\xA9.pak");
	const fixture::bytes before = fixture::read(utf8_cafe);
	CHECK(PAKLib::repair_file(utf8_cafe, dir + "CAF\xC3\x89.pak").success());
	CHECK(fixture::read(utf8_cafe) == before);
#else
	const std::string utf8_cafe = repair_input("caf\xC3\xA9.pak");
	CHECK_THROWS_AS(PAKLib::repair_file(utf8_cafe, dir + "CAF\xC3\x89.pak"), std::invalid_argument, must_differ);
#endif
}

TEST_CASE("Paths: relative, absolute, dot segments, case and slashes resolve to the same file") {
	const std::string input = repair_input("path_cmp.pak");                    // ./TestOutput/path_cmp.pak
	const std::string absolute = fixture::absolute(input);
	const std::vector<std::string> same = {
		input,
		absolute,
		"TestOutput/path_cmp.pak",
		"./TestOutput/./path_cmp.pak",
		"TestOutput/../TestOutput/path_cmp.pak",
		"TestOutput/not_a_dir/../path_cmp.pak",                                // lexical: the directory need not exist
		absolute.substr(0, absolute.size() - std::string("path_cmp.pak").size()) + "x/../path_cmp.pak",
		ascii_upper(input),
		ascii_upper(absolute),
		replace_all(input, '/', '\\'),
		replace_all(absolute, '\\', '/'),
		".\\TESTOUTPUT\\.\\Path_Cmp.PAK",
	};
	for (const std::string& variant : same) {
		CHECK_THROWS_AS(PAKLib::repair_file(input, variant), std::invalid_argument, must_differ);
		CHECK_THROWS_AS(PAKLib::repair_file(absolute, variant), std::invalid_argument, must_differ);
	}
	// Different files, including ones that do not exist yet, are accepted
	CHECK(PAKLib::repair_file(input, "TestOutput/path_cmp_out.pak").success());
	CHECK(PAKLib::repair_file("./TestOutput/../TestOutput/path_cmp.pak", absolute + ".out").success());
	CHECK(fixture::read("TestOutput/path_cmp_out.pak") == fixture::read(absolute + ".out"));
}
