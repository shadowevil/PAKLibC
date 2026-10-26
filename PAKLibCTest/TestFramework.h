#pragma once
#include <cstdio>
#include <exception>
#include <functional>
#include <string>
#include <vector>

// Minimal self-registering test framework for the PAKLibC test scenarios.
// TEST_CASE bodies run in registration order; CHECK records a failure and keeps going,
// REQUIRE records a failure and stops the current test case.
namespace test {
	struct test_case {
		const char* name;
		const char* file;
		std::function<void()> body;
	};

	struct abort_case {};

	inline std::vector<test_case>& registry() {
		static std::vector<test_case> cases;
		return cases;
	}

	inline int& failure_count() {
		static int failures = 0;
		return failures;
	}

	// Folder holding Amp.pak; fixtures are written to <data_dir>/TestOutput
	inline std::string& data_dir() {
		static std::string dir = ".";
		return dir;
	}

	struct registrar {
		registrar(const char* name, const char* file, std::function<void()> body) {
			registry().push_back({ name, file, body });
		}
	};

	inline void fail(const char* file, int line, const std::string& what) {
		++failure_count();
		std::printf("    FAIL %s(%d): %s\n", file, line, what.c_str());
	}

	inline bool contains(const std::string& text, const std::string& part) {
		return text.find(part) != std::string::npos;
	}
}

#define TEST_CONCAT_INNER(a, b) a##b
#define TEST_CONCAT(a, b) TEST_CONCAT_INNER(a, b)

#define TEST_CASE(name) \
	static void TEST_CONCAT(test_body_, __LINE__)(); \
	static test::registrar TEST_CONCAT(test_reg_, __LINE__)(name, __FILE__, TEST_CONCAT(test_body_, __LINE__)); \
	static void TEST_CONCAT(test_body_, __LINE__)()

#define CHECK(cond) \
	do { if (!(cond)) test::fail(__FILE__, __LINE__, "CHECK(" #cond ")"); } while (0)

#define REQUIRE(cond) \
	do { if (!(cond)) { test::fail(__FILE__, __LINE__, "REQUIRE(" #cond ")"); throw test::abort_case{}; } } while (0)

// Expects expr to throw std::exception whose what() contains message_part
#define CHECK_THROWS_WITH(expr, message_part) \
	do { \
		bool thrown_ = false; \
		try { (void)(expr); } \
		catch (const std::exception& ex_) { \
			thrown_ = true; \
			if (!test::contains(ex_.what(), message_part)) \
				test::fail(__FILE__, __LINE__, std::string("wrong message: '") + ex_.what() + "', expected '" + (message_part) + "'"); \
		} \
		if (!thrown_) test::fail(__FILE__, __LINE__, "expected exception from: " #expr); \
	} while (0)

#define CHECK_NOTHROW(expr) \
	do { \
		try { (void)(expr); } \
		catch (const std::exception& ex_) { test::fail(__FILE__, __LINE__, std::string("unexpected exception: ") + ex_.what()); } \
	} while (0)
