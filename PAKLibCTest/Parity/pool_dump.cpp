// Writes PAKLibC's parity report for a list of inputs: pool_dump <dir> <list> <out>
// The C++ side of regeneration step 2 (see README.md); the .NET harness takes the same three arguments. Not part of
// the test project: build it on its own, for example
//   cl /nologo /EHsc /O2 /std:c++17 /I PAKLibC /I PAKLibCTest\Parity /Fe<build dir>\pool_dump.exe PAKLibCTest\Parity\pool_dump.cpp
#include "ParityReport.h"
#include <fstream>

int main(int argc, char** argv) {
	if (argc < 4)
		return 2;
	std::ifstream list(argv[2]);
	std::ofstream out(argv[3], std::ios::binary);
	std::string name;
	while (std::getline(list, name)) {
		if (!name.empty() && name.back() == '\r')
			name.pop_back();
		if (name.empty())
			continue;
		out << parity::report(name, PAKLib::detail::read_all_bytes(std::string(argv[1]) + "/" + name));
	}
	return out ? 0 : 1;
}
