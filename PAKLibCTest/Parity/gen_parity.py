"""Builds and maintains the parity corpus (see README.md for the workflow).

  python gen_parity.py check      --cs A.txt --cpp B.txt --pool DIR
        Classifies every difference between a C# and a C++ report of the inputs in DIR; exits 1 on any unclassified
        difference.
  python gen_parity.py select     --pool DIR --list LIST --cs REPORT [--budget BYTES] [--must NAME ...]
        Picks the smallest set of pool files that still shows every behaviour class (normalised report line) of the
        C# report, within the byte budget, and copies them to cases/ (cases.txt lists them).
  python gen_parity.py expected   --harness EXE
        Runs the .NET 8 harness over cases/ and writes expected.txt.
  python gen_parity.py deviations --actual REPORT
        Compares expected.txt with the C++ report the replay test wrote (Build/TestOutput/parity_actual.txt) and
        writes deviations.txt: every differing hunk with its class. Exits 1 on any unclassified difference.

Printed output is ASCII. Reports are UTF-8 and compared byte for byte."""
import argparse, difflib, os, re, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
CASES = os.path.join(HERE, "cases")
LIST = os.path.join(HERE, "cases.txt")
EXPECTED = os.path.join(HERE, "expected.txt")
DEVIATIONS = os.path.join(HERE, "deviations.txt")

# bounds-checked-read (ruling: counts uncapped but bounded by the data size). PAKLibC checks every count and length
# against the data first and throws invalid_data_error; PAKLib instead either
#  (a) lets an incidental .NET exception escape from BinaryReader / Array / List. Each exception type is accepted only
#      for the PAKLibC messages it can stand for, and never for an image that runs past the end; or
#  (b) reads a short image: BinaryReader.ReadBytes returns what is left. PAKLib then accepts it (the last sprite:
#      "read OK"), fails its own position check at the next entry, or rejects the cut bytes as no BMP / PNG. Each is
#      accepted only when the input itself proves the cut: the sprite's table entry ends past the end of the file.
INCIDENTAL = {
    "EndOfStreamException": ("Unexpected end of data at offset ", "does not fit in the data"),
    "ArgumentOutOfRangeException": (" is negative.", "has a negative offset", "does not fit in the data"),
    "OutOfMemoryException": ("does not fit in the data",),
    "OverflowException": ("does not fit in the data",),
}
SHORT_IMAGE = re.compile(r"^read ERR InvalidDataException: Sprite (\d+): image length (\d+) runs past the end of the data\.$")
SHORT_IMAGE_CS = ("read ERR Exception: Expected to be at position ",
                  "read ERR InvalidDataException: Invalid sprite data format. Expected BMP or PNG.")
PALETTE = "puts the palette outside the data."
INDEX_OOR = "IndexOutOfRangeException: Index was outside the bounds of the array."


def a(s):
    return s.encode("ascii", "replace").decode("ascii").replace("\x00", "\\0")


def read_blocks(path):
    """{name: [lines]} and order, from a report ('== name' headers). Bytes are kept exact (UTF-8, surrogateescape)."""
    text = open(path, "rb").read().decode("utf-8", "surrogateescape")
    blocks, order, cur = {}, [], None
    for line in text.split("\n"):
        line = line[:-1] if line.endswith("\r") else line
        if line.startswith("== "):
            cur = line[3:]
            blocks[cur] = []
            order.append(cur)
        elif cur is not None and line != "":
            blocks[cur].append(line)
    return order, blocks


def read_part_end(lines):
    for i, l in enumerate(lines):
        if l.startswith("repair "):
            return i
    return len(lines)


def entry_runs_past(data, n):
    """True when sprite table entry n of the PAK bytes is well formed and ends past the end of the data."""
    if data is None or len(data) < 24 + 8 * (n + 1):
        return False
    count = int.from_bytes(data[20:24], "little", signed=True)
    if n >= count:
        return False
    offset = int.from_bytes(data[24 + 8 * n:28 + 8 * n], "little", signed=True)
    size = int.from_bytes(data[28 + 8 * n:32 + 8 * n], "little", signed=True)
    return offset >= 0 and size >= 0 and offset + size > len(data)


def incidental(A, B):
    """(a): one C# exception line (plus its message's continuation lines) from INCIDENTAL, paired with its message."""
    if len(B) != 1 or not A:
        return False
    m = re.match(r"^read ERR (\w+): ", A[0])
    if not m or m.group(1) not in INCIDENTAL or not B[0].startswith("read ERR InvalidDataException: "):
        return False
    if any(x.startswith(("read ", "  r=", "tobytes ", "repair ")) for x in A[1:]):
        return False
    return any(t in B[0] for t in INCIDENTAL[m.group(1)])


def short_image(A, B, data):
    """(b): PAKLib read a cut image of sprite n and the input proves the cut."""
    if len(B) != 1 or not A:
        return False
    m = SHORT_IMAGE.match(B[0])
    if not m or not entry_runs_past(data, int(m.group(1))):
        return False
    n = int(m.group(1))
    if len(A) == 1 and A[0].startswith(SHORT_IMAGE_CS):
        return True
    k = re.match(r"^read OK n=(\d+)$", A[0])
    if not k or int(k.group(1)) != n + 1 or len(A) != n + 3:
        return False                      # only the last sprite can be cut and still read OK
    return all(x.startswith("  r=") for x in A[1:-1]) and A[-1].startswith("tobytes ")


def classify(cs, cpp, data=None):
    """Hunks (i1, i2, cs_lines, cpp_lines, class) turning cs into cpp; class None when unexplained.
    data: the input's bytes (needed to accept a short image read)."""
    out = []
    sm = difflib.SequenceMatcher(a=cs, b=cpp, autojunk=False)
    read_end = read_part_end(cs)
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == "equal":
            continue
        A, B = cs[i1:i2], cpp[j1:j2]
        cls = None
        if len(A) == len(B) and all(x.replace("\\x00", "?") == y for x, y in zip(A, B)):
            cls = "nul-in-message"            # PAKLibC maps NUL in echoed chunk types to '?', as it does bytes > 0x7F
        elif A and all(INDEX_OOR in x for x in A) and any(PALETTE in y for y in B):
            cls = "palette-offset-overflow"   # SpriteImage's int overflow (IndexOutOfRange) is invalid data in PAKLibC
        elif i1 == 0 and i2 <= read_end and (incidental(A, B) or short_image(A, B, data)):
            cls = "bounds-checked-read"       # see INCIDENTAL / SHORT_IMAGE above
        elif (i2 <= read_end and len(A) == 1 and len(B) == 1
              and (A[0].startswith("read ERR ArgumentException: ") or A[0].startswith("read ERR ArgumentOutOfRangeException: "))
              and B[0] == "read ERR InvalidDataException: Invalid sprite data format. Expected BMP or PNG."):
            cls = "bmp-under-26-bytes"        # IsValidBmp reads past a 14-25 byte BMP; PAKLibC requires 26 bytes (ruling)
        out.append((i1, i2, A, B, cls))
    return out


def read_input(directory, name):
    path = os.path.join(directory, name)
    return open(path, "rb").read() if name.endswith(".pak") and os.path.isfile(path) else None


def cmd_check(args):
    order, cs = read_blocks(args.cs)
    _, cpp = read_blocks(args.cpp)
    counts, bad = {}, []
    same = 0
    for name in order:
        if name not in cpp:
            bad.append((name, [("missing in C++", "")]))
            continue
        if cs[name] == cpp[name]:
            same += 1
            continue
        hunks = classify(cs[name], cpp[name], read_input(args.pool, name))
        classes = set(h[4] for h in hunks)
        if None in classes:
            bad.append((name, [(x, y) for h in hunks if h[4] is None for x, y in zip(h[2] + [""] * len(h[3]), h[3] + [""] * len(h[2]))]))
        for c in classes:
            counts[c] = counts.get(c, 0) + 1
    print("blocks %d, identical %d, deviating %d, unexplained %d" % (len(order), same, len(order) - same, len(bad)))
    for c in sorted(k for k in counts if k):
        print("   %-26s %d" % (c, counts[c]))
    for name, pairs in bad[:30]:
        print("   UNEXPLAINED " + a(name))
        for x, y in pairs[:8]:
            print("      C#:  " + a(x))
            print("      C++: " + a(y))
    return 1 if bad else 0


def features(lines):
    f = set()
    for l in lines:
        n = re.sub(r"\b[0-9a-f]{16}\b", "H", l)
        n = re.sub(r"[0-9]+", "#", n)
        f.add(n)
    return f


def cmd_select(args):
    order, cs = read_blocks(args.cs)
    names = [l.strip() for l in open(args.list, encoding="ascii") if l.strip()]
    sizes = {n: os.path.getsize(os.path.join(args.pool, n)) for n in names}
    text = {n: sum(len(l.encode("utf-8", "surrogateescape")) + 1 for l in cs.get(n, [])) + len(n) + 4 for n in names}
    feats = {n: features(cs.get(n, [])) for n in names}
    chosen, covered, used = [], set(), 0
    for n in args.must or []:
        chosen.append(n)
        covered |= feats[n]
        used += sizes[n] + text[n]
    all_feats = set().union(*feats.values())
    while True:
        best, best_score = None, 0.0
        for n in names:
            if n in chosen:
                continue
            new = len(feats[n] - covered)
            if new == 0 or used + sizes[n] + text[n] > args.budget:
                continue
            score = new / (sizes[n] + text[n] + 256)
            if score > best_score:
                best, best_score = n, score
        if best is None:
            break
        chosen.append(best)
        covered |= feats[best]
        used += sizes[best] + text[best]
    missing = all_feats - covered
    print("chosen %d of %d files, %d bytes (inputs + expected text), classes covered %d of %d" %
          (len(chosen), len(names), used, len(covered), len(all_feats)))
    for m in sorted(missing)[:20]:
        print("   NOT COVERED (budget): " + a(m))
    if os.path.isdir(CASES):
        for f in os.listdir(CASES):
            os.remove(os.path.join(CASES, f))
    else:
        os.makedirs(CASES)
    chosen.sort()
    for n in chosen:
        shutil.copyfile(os.path.join(args.pool, n), os.path.join(CASES, n))
    with open(LIST, "w", encoding="ascii", newline="\n") as f:
        f.write("\n".join(chosen) + "\n")
    return 0 if not missing else 1


def cmd_expected(args):
    subprocess.run([os.path.abspath(args.harness), CASES, LIST, EXPECTED], check=True, timeout=3600)
    order, _ = read_blocks(EXPECTED)
    print("expected.txt: %d cases" % len(order))
    return 0


def cmd_deviations(args):
    order, cs = read_blocks(EXPECTED)
    _, cpp = read_blocks(args.actual)
    lines, bad, count = [], 0, 0
    for name in order:
        if name not in cpp:
            print("missing in C++: " + a(name))
            bad += 1
            continue
        if cs[name] == cpp[name]:
            continue
        for i1, i2, A, B, cls in classify(cs[name], cpp[name], read_input(CASES, name)):
            if cls is None:
                bad += 1
                print("UNEXPLAINED " + a(name))
                for x in A:
                    print("   C#:  " + a(x))
                for y in B:
                    print("   C++: " + a(y))
                continue
            count += 1
            lines.append("@@ %s %d %d %s" % (name, i1, i2, cls))
            lines += ["- " + x for x in A]
            lines += ["+ " + y for y in B]
    if bad:
        print("%d unexplained differences: deviations.txt not written" % bad)
        return 1
    header = ["# Expected differences between PAKLib (expected.txt, .NET 8) and PAKLibC, generated by gen_parity.py.",
              "# '@@ <case> <first> <end> <class>' replaces expected lines [first, end) of that case's block with the '+'",
              "# lines. Classes: bounds-checked-read, bmp-under-26-bytes, palette-offset-overflow, nul-in-message (see README.md)."]
    with open(DEVIATIONS, "wb") as f:
        f.write(("\n".join(header + lines) + "\n").encode("utf-8", "surrogateescape"))
    print("deviations.txt: %d hunks" % count)
    return 0


def main():
    p = argparse.ArgumentParser()
    sub = p.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("check"); c.add_argument("--cs", required=True); c.add_argument("--cpp", required=True)
    c.add_argument("--pool", required=True)
    s = sub.add_parser("select"); s.add_argument("--pool", required=True); s.add_argument("--list", required=True)
    s.add_argument("--cs", required=True); s.add_argument("--budget", type=int, default=1900000); s.add_argument("--must", nargs="*")
    e = sub.add_parser("expected"); e.add_argument("--harness", required=True)
    d = sub.add_parser("deviations"); d.add_argument("--actual", required=True)
    args = p.parse_args()
    return {"check": cmd_check, "select": cmd_select, "expected": cmd_expected, "deviations": cmd_deviations}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
