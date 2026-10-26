# Parity corpus

PAKLibC (`PAKLibC\PAK.h`) is a 1:1 port of PAKLib 2.0 (C#). This folder proves it on real inputs: every file in
`cases\` has a reference report written by PAKLib itself on **.NET 8**, and the test `TestsParity.cpp` replays each
input through PAK.h and must reproduce that report line for line.

| File | What it is |
| --- | --- |
| `cases\` | the inputs: `*.pak` (PAK data, often damaged) and `*.img` (sprite images) |
| `cases.txt` | the case list, in order |
| `expected.txt` | PAKLib's report for every case (generated; do not edit) |
| `deviations.txt` | the only allowed differences, one classified hunk each (generated; do not edit) |
| `harness\` | the .NET 8 harness: compiles PAKLib's own sources by relative path and writes `expected.txt` |
| `ParityReport.h` | the C++ twin of the harness, used by the replay test |
| `pool_dump.cpp` | a stand-alone C++ report writer over a list of inputs (regeneration step 2) |
| `gen_parity.py` | selects cases, writes `expected.txt` and `deviations.txt`, classifies differences |
| `api_map.txt`, `check_api.py` | every public PAKLib type and member mapped to its PAKLibC counterpart, and the check |

## What a report holds

For a `.pak` case: `PAKData.Read` (result or exception type and message; per sprite the rectangles hash, color key,
image hash and the decoded pixel hash), `ToBytes` (hash and length), `PAKRepair.Repair` on the whole file without and
with force, and on four sections with force (each: success, placeholders, the full issue list, the sprites and the
rebuilt bytes' hash). For a `.img` case: format, BMP bit depth, `TryGetSize`, `CheckSize`, the decoded pixel hash, and
`ToBmp24` / `ToPng` with several color keys. Hashes are FNV-1a 64. Control characters that PAKLib echoes from PNG
chunk types are written as `\xHH` by both sides, so every file here is plain ASCII.

## Allowed deviations

`deviations.txt` lists, per case, which expected lines are replaced by which C++ lines and why. The replay checks
that each hunk still matches `expected.txt`, so a stale entry fails too. `gen_parity.py` refuses to write the file
when any difference is not one of these classes:

* `bounds-checked-read`: PAKLibC checks every count and length against the data first and throws
  `invalid_data_error` with a message that says what does not fit (ruling: counts uncapped but bounded by the data
  size). Only two PAKLib outcomes of the read (the first report line onward, before the repair lines) may stand
  against it:
  (a) an incidental `EndOfStreamException`, `ArgumentOutOfRangeException`, `OutOfMemoryException` or
  `OverflowException`, each accepted only for the PAKLibC messages it can stand for (never for an image that runs
  past the end); or
  (b) a short image read: `BinaryReader.ReadBytes` returns what is left, and PAKLib accepts it (only the last
  sprite can, giving `read OK`), fails its own position check at the next entry, or rejects the cut bytes as no
  BMP / PNG. This is accepted only when the input proves the cut: that sprite's table entry ends past the end of
  the file. A C++ reader that wrongly reports an image as cut therefore shows up as an unexplained difference.
* `bmp-under-26-bytes`: PAKLib's `IsValidBmp` reads past a 14-25 byte BMP and throws; PAKLibC requires 26 bytes and
  reports the invalid image format (ruling: the quirk is not ported).
* `palette-offset-overflow`: `SpriteImage` computes `14 + dibSize` in `int`; from `0x7FFFFFF2` it wraps and the
  palette read throws `IndexOutOfRangeException` (which also escapes `PAKRepair`). PAKLibC throws
  `invalid_data_error` ("BMP header size N puts the palette outside the data.") at the same point.
* `nul-in-message`: PAKLib echoes a NUL from a PNG chunk type into its message; PAKLibC writes `?`, as it does for
  bytes above 0x7F, so `what()` never holds an embedded NUL.

Everything else, including every zlib error text and the exact point where .NET 8's zlib reports or ignores
corrupt data past the image size (see the inflate notes in PAK.h), must match.

## Regenerating

Requirements: the .NET 8 runtime and an SDK that can build `net8.0`, Python 3, and PAKLib's sources at
`..\..\..\PAKLib\PAKLib` (the harness compiles them in; nothing is written there). Paths below are relative to the
repository root; keep build output outside the tree with `--artifacts-path`.

1. Build the harness:
   `dotnet build PAKLibCTest\Parity\harness -c Release --artifacts-path <build dir>\harness`
2. Optional, to re-pick cases. This step needs a pool of candidate inputs, which is not in the repository (the
   pool behind the current cases was 5,589 files, about 23 MB; it is not reproducible from here). Any folder of
   `*.pak` and `*.img` files serves, with a list file naming them one per line; `cases\` itself is a valid
   starting pool, to which new damaged or crafted files are added. Write both reports over it:
   `<harness exe> <pool> <list> <cs report>` and, with `pool_dump.cpp` built as its header comment shows,
   `pool_dump.exe <pool> <list> <cpp report>`. Check them
   (`python PAKLibCTest\Parity\gen_parity.py check --cs <cs report> --cpp <cpp report> --pool <pool>`, which must
   show no unexplained difference), then
   `python PAKLibCTest\Parity\gen_parity.py select --pool <pool> --list <list> --cs <cs report> --must <names>`.
   It keeps the `--must` files and adds the cheapest files until every behaviour class (normalised report line) of
   the pool is covered, within the byte budget (default 1.9 MB). Pass every name in `cases.txt` as `--must` to
   keep the current cases and only add to them.
3. Write the reference report:
   `python PAKLibCTest\Parity\gen_parity.py expected --harness <build dir>\harness\bin\ParityHarness\release\ParityHarness.exe`
4. Run the test suite once (from `PAKLibCTest\Build`). The replay writes PAKLibC's report to
   `Build\TestOutput\parity_actual.txt`; it fails while `deviations.txt` is out of date.
5. Write the deviations, then run the suite again; it must pass:
   `python PAKLibCTest\Parity\gen_parity.py deviations --actual PAKLibCTest\Build\TestOutput\parity_actual.txt`

`python PAKLibCTest\Parity\check_api.py` checks the API map against PAKLib's sources and PAK.h (exit 1 on any
unmapped, stale or dangling entry). It checks names, not meaning: a mapping that names an existing but wrong
PAK.h member, or a `(none)` entry for a member that does have a counterpart, passes. Those lines are reviewed by
hand.

## Where the cases came from

The 271 cases were picked from 5,589 candidates: the first-round cross-check corpus, the read/write/image and repair
review corpora (mutated and adversarial files), the zlib experiments used to pin down .NET 8's inflate behaviour, the
unit tests' scenario files, and `Amp.pak`. Over all 5,589 the C# and C++ reports differ only in the four classes above.
Fourteen repair cases are kept on purpose (`--must`): `rp_contig.pak`, `rp_r0447`, `r0448`, `r0514`, `r0556`,
`r0576`, `r0737`, `r0933`, `r1282`, the four `rp_w_count_magic*` files (a true header with magic and a table entry
but a damaged count, and a later false candidate with count 1 and a table entry, which PAKLib picks 5 to 4), and
`rp_w_bmp_size_plus1.pak` (a BMP whose size field is one byte over the data). They pin the entry-start scoring of
`PAKRepair` (a count match 3, the header magic 2, a table entry 2, contiguity 3, ties to the larger 12-byte count)
and the BMP size check: the contiguity weight at 2, magic at 1 or 3, table at 3, count at 2 or 4, a reversed
tie-break, or a BMP size check one byte loose, each fails the replay. Table 1 and contiguity 4 change no report over
the whole pool, so no input here tells them apart.
Case names keep their source as a prefix (`p1_`, `rw_`, `rp_`, `two_`, `ut_`, `real_`).
