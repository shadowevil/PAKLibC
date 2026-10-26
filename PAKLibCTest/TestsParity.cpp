// Parity corpus replay: every input in Parity\cases goes through PAK.h and must produce PAKLib's report
// (Parity\expected.txt, written by the .NET 8 harness) line for line, except for the hunks listed in
// Parity\deviations.txt, each of which must still match the expected lines it replaces. See Parity\README.md.
#include "PakFixture.h"
#include "Parity/ParityReport.h"
#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>

namespace {
	struct hunk {
		size_t first = 0, end = 0;
		std::string cls;
		std::vector<std::string> removed, added;
	};

	std::string parity_dir() {
		return test::data_dir() + "/../Parity";
	}

	std::vector<std::string> split_lines(const std::string& text) {
		std::vector<std::string> lines;
		size_t at = 0;
		while (at < text.size()) {
			size_t next = text.find('\n', at);
			if (next == std::string::npos)
				next = text.size();
			std::string line = text.substr(at, next - at);
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			lines.push_back(line);
			at = next + 1;
		}
		return lines;
	}

	std::string read_text(const std::string& path) {
		const fixture::bytes data = fixture::read(path);
		return std::string(data.begin(), data.end());
	}

	// "== name" blocks, in order
	std::vector<std::pair<std::string, std::vector<std::string>>> blocks(const std::string& text) {
		std::vector<std::pair<std::string, std::vector<std::string>>> out;
		for (const std::string& line : split_lines(text)) {
			if (line.compare(0, 3, "== ") == 0)
				out.emplace_back(line.substr(3), std::vector<std::string>());
			else if (!out.empty() && !line.empty())
				out.back().second.push_back(line);
		}
		return out;
	}

	std::map<std::string, std::vector<hunk>> read_deviations(const std::string& text) {
		std::map<std::string, std::vector<hunk>> out;
		hunk* current = nullptr;
		for (const std::string& line : split_lines(text)) {
			if (line.empty() || line[0] == '#')
				continue;
			if (line.compare(0, 3, "@@ ") == 0) {
				std::istringstream fields(line.substr(3));
				std::string name;
				hunk h;
				fields >> name >> h.first >> h.end >> h.cls;
				out[name].push_back(h);
				current = &out[name].back();
			}
			else if (current != nullptr && line.size() >= 2 && line[1] == ' ' && (line[0] == '-' || line[0] == '+')) {
				(line[0] == '-' ? current->removed : current->added).push_back(line.substr(2));
			}
		}
		return out;
	}

	std::string printable(const std::string& s) {
		std::string out;
		for (unsigned char c : s) {
			if (c == 0)
				out += "\\0";
			else if (c < 0x20 || c > 0x7E)
				out += '?';
			else
				out += static_cast<char>(c);
		}
		return out;
	}
}

TEST_CASE("Parity: every corpus input matches PAKLib's report (with only the listed deviations)") {
	const auto expected = blocks(read_text(parity_dir() + "/expected.txt"));
	const auto deviations = read_deviations(read_text(parity_dir() + "/deviations.txt"));
	const std::vector<std::string> listed = split_lines(read_text(parity_dir() + "/cases.txt"));
	REQUIRE(expected.size() >= 200);
	size_t listed_count = 0;
	for (const std::string& name : listed)
		if (!name.empty())
			++listed_count;
	CHECK(listed_count == expected.size());

	std::string actual_all;
	size_t identical = 0, deviating = 0, used_hunks = 0, total_hunks = 0;
	for (const auto& d : deviations)
		total_hunks += d.second.size();
	for (const auto& block : expected) {
		const std::string& name = block.first;
		const std::string report = parity::report(name, fixture::read(parity_dir() + "/cases/" + name));
		actual_all += report;
		std::vector<std::string> actual = split_lines(report);
		actual.erase(actual.begin());   // the "== name" line
		while (!actual.empty() && actual.back().empty())
			actual.pop_back();

		// Expected lines, with this case's hunks applied (last first, so earlier indices stay valid)
		std::vector<std::string> want = block.second;
		const auto found = deviations.find(name);
		bool hunks_ok = true;
		if (found != deviations.end()) {
			++deviating;
			std::vector<hunk> hunks = found->second;
			std::sort(hunks.begin(), hunks.end(), [](const hunk& x, const hunk& y) { return x.first > y.first; });
			for (const hunk& h : hunks) {
				++used_hunks;
				const bool in_range = h.first <= h.end && h.end <= want.size();
				const bool matches = in_range && std::vector<std::string>(want.begin() + static_cast<std::ptrdiff_t>(h.first),
					want.begin() + static_cast<std::ptrdiff_t>(h.end)) == h.removed;
				if (!matches) {
					hunks_ok = false;
					test::fail(__FILE__, __LINE__, "deviation hunk for " + name + " (" + h.cls + ") no longer matches expected.txt");
					continue;
				}
				want.erase(want.begin() + static_cast<std::ptrdiff_t>(h.first), want.begin() + static_cast<std::ptrdiff_t>(h.end));
				want.insert(want.begin() + static_cast<std::ptrdiff_t>(h.first), h.added.begin(), h.added.end());
			}
		}
		else {
			++identical;
		}
		if (hunks_ok && actual != want) {
			size_t i = 0;
			while (i < actual.size() && i < want.size() && actual[i] == want[i])
				++i;
			test::fail(__FILE__, __LINE__, "parity case " + name + " differs at line " + std::to_string(i + 1) +
				"\n        expected: " + (i < want.size() ? printable(want[i]) : "<end>") +
				"\n        actual:   " + (i < actual.size() ? printable(actual[i]) : "<end>"));
		}
	}
	CHECK(used_hunks == total_hunks);
	CHECK(deviations.size() <= expected.size());

	// The full C++ report, for gen_parity.py deviations (see README.md)
	std::ofstream out(fixture::output_dir() + "/parity_actual.txt", std::ios::binary);
	out << actual_all;
	std::printf("    parity: %u cases, %u identical, %u with listed deviations (%u hunks)\n",
		static_cast<unsigned>(expected.size()), static_cast<unsigned>(identical), static_cast<unsigned>(deviating), static_cast<unsigned>(total_hunks));
}
