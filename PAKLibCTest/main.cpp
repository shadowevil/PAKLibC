// PAKLibC test runner.
// Usage: PAKLibCTest [data_dir] [name filter]
//   data_dir     folder holding Amp.pak (default: the working directory, which Visual Studio sets to Build\)
//   name filter  only run test cases whose name contains this text
// Exit code is 0 when every check passed, 1 when any failed or the filter matched no test case.
#include "TestFramework.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

int main(int argc, char** argv)
{
#if defined(_MSC_VER) && defined(_DEBUG)
	// Debug assertions print to stderr and abort with a non-zero exit instead of opening a dialog
	_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
	_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
	_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
	_CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
	_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

	if (argc > 1)
		test::data_dir() = argv[1];
	const std::string filter = argc > 2 ? argv[2] : "";

	int ran = 0;
	int failed_cases = 0;
	for (const test::test_case& tc : test::registry()) {
		if (!filter.empty() && !test::contains(tc.name, filter))
			continue;
		++ran;
		const int before = test::failure_count();
		std::printf("[ RUN  ] %s\n", tc.name);
		try {
			tc.body();
		}
		catch (const test::abort_case&) {
		}
		catch (const std::exception& ex) {
			test::fail(tc.file, 0, std::string("unexpected exception: ") + ex.what());
		}
		catch (...) {
			test::fail(tc.file, 0, "unexpected non-standard exception");
		}
		const bool passed = test::failure_count() == before;
		if (!passed)
			++failed_cases;
		std::printf("[ %s ] %s\n", passed ? " OK " : "FAIL", tc.name);
	}

	std::printf("\n%d test case(s), %d failed, %d failed check(s)\n", ran, failed_cases, test::failure_count());
	if (ran == 0) {
		std::printf("no test case matched the filter '%s'\n", filter.c_str());
		return 1;
	}
	return test::failure_count() == 0 ? 0 : 1;
}
