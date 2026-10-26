# PAKLibC

C++14 port of the C# library PAKLib (`C:\Users\ShadowEvil\source\repos\PAKLib`), which is the
reference for the PAK format and its read rules.

## Rules

- **PAK.h is a single header file.** "PAK.h is supposed to be a single header file, so treat that as
  the standard" (user rule, 2026-10-26). All library code lives in `PAKLibC/PAK.h`: no companion
  `.cpp` or `.h` files for the library and nothing that has to be compiled separately. Everything in
  it is `inline` or a template, so it can be included from any number of translation units.
  Hooks SH1-SH3 (log-only, `.claude/hooks/RULES.md`) flag companion files and `ClCompile` items for
  the library; the test project's multi-file build catches non-inline definitions at link time.
