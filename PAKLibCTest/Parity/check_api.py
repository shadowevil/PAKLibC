"""API parity check: every public PAKLib (C#) type and member must have a line in api_map.txt, every line there
must still name a public PAKLib member (no stale entries), and every C++ name on the right must exist in PAK.h.

  python check_api.py [--paklib DIR] [--header PAK.h]

PAKLib's sources default to ..\\..\\..\\PAKLib\\PAKLib (relative to this folder), PAK.h to ..\\..\\PAKLibC\\PAK.h.
Public members of public top-level types are scraped; private / internal types (and everything nested in them)
and internal members are ignored. Overloads are keyed by declared parameter count: Type.Member(n).
Exits 1 on any unmapped, stale, duplicate or dangling entry. Output is ASCII."""
import argparse, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))


def strip_comments_and_strings(src):
    src = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), src, flags=re.S)
    src = re.sub(r"//[^\n]*", "", src)
    src = re.sub(r'@?\$?"(?:[^"\\\n]|\\.)*"', '""', src)      # string literals (keep braces inside them out)
    src = re.sub(r"'(?:[^'\\\n]|\\.)'", "''", src)
    return src


def param_count(params):
    params = params.strip()
    if not params:
        return 0
    depth, count = 0, 1
    for c in params:
        if c in "<([":
            depth += 1
        elif c in ">)]":
            depth -= 1
        elif c == "," and depth == 0:
            count += 1
    return count


TYPE_RE = re.compile(r"^\s*public\s+(?:static\s+|sealed\s+|abstract\s+|partial\s+|readonly\s+)*(class|struct|enum|interface|record)\s+(\w+)")
NESTED_TYPE_RE = re.compile(r"^\s*(?:public|private|internal|protected)?\s*(?:static\s+|sealed\s+|abstract\s+|partial\s+|readonly\s+)*(class|struct|enum|interface|record)\s+(\w+)")
MEMBER_RE = re.compile(r"^\s*public\s+(?!class\b|struct\b|enum\b|interface\b|record\b)(.*)$")


def scrape(paklib):
    """Set of 'Type' and 'Type.Member[(n)]' keys."""
    keys = {}
    files = []
    for root, _, names in os.walk(paklib):
        if os.sep + "obj" in root or os.sep + "bin" in root:
            continue
        files += [os.path.join(root, n) for n in names if n.endswith(".cs")]
    for path in sorted(files):
        lines = strip_comments_and_strings(open(path, encoding="utf-8-sig").read()).split("\n")
        depth = 0
        stack = []          # (type name or None for a non-public / nested type, depth at which its body opens)
        pending = None      # type declared on this line, body opens at the next '{'
        for raw in lines:
            line = raw
            m_type = TYPE_RE.match(line)
            m_nested = NESTED_TYPE_RE.match(line)
            in_public_type = stack and stack[-1][0] is not None and depth == stack[-1][1]
            if m_type and not stack:
                pending = (m_type.group(2), m_type.group(1))
                keys[m_type.group(2)] = path
            elif m_nested and stack:
                pending = (None, m_nested.group(1))     # nested types: members not part of PAKLib's API
            elif in_public_type and stack[-1][2] == "enum":
                for ident in re.findall(r"\b([A-Z]\w*)\b(?:\s*=\s*[^,]+)?\s*,?", line):
                    keys[stack[-1][0] + "." + ident] = path
            elif in_public_type:
                m = MEMBER_RE.match(line)
                if m:
                    decl = m.group(1)
                    tname = stack[-1][0]
                    ctor = re.match(r"(?:static\s+)?" + re.escape(tname) + r"\s*\(([^)]*)\)", decl)
                    method = re.search(r"(\w+)\s*(?:<[^>]*>)?\s*\(([^)]*)\)", decl)
                    if ctor:
                        name = "ctor"
                        n = param_count(ctor.group(1))
                    elif method and "=>" not in decl.split("(")[0] and "=" not in decl.split("(")[0]:
                        name = method.group(1)
                        n = param_count(method.group(2))
                    else:
                        m2 = re.search(r"(\w+)\s*(?:\{|=>|=|;|$)", decl)
                        name, n = (m2.group(1) if m2 else decl.strip()), None
                        if name == "override":
                            name = decl.split()[-1]
                    if name.startswith("operator"):
                        continue
                    keys["%s.%s" % (tname, name) if n is None else "%s.%s(%d)" % (tname, name, n)] = path
            # braces
            for c in line:
                if c == "{":
                    depth += 1
                    if pending is not None:
                        stack.append((pending[0], depth, pending[1]))
                        pending = None
                elif c == "}":
                    if stack and stack[-1][1] == depth:
                        stack.pop()
                    depth -= 1
    # an overloaded member keeps the (n) suffix; a single one drops it
    plain = {}
    for k in keys:
        base = re.sub(r"\(\d+\)$", "", k)
        plain.setdefault(base, []).append(k)
    out = set()
    for base, variants in plain.items():
        if len(variants) == 1:
            out.add(base)
        else:
            out.update(variants)
    return out


def read_map(path):
    entries, extras, errors = {}, [], []
    for no, raw in enumerate(open(path, encoding="ascii"), 1):
        line = raw.rstrip("\r\n")
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        if line.startswith("extra:"):
            extras.append((no, line[6:].strip()))
            continue
        if "->" not in line:
            errors.append("api_map.txt:%d: no '->' in: %s" % (no, line))
            continue
        left, right = line.split("->", 1)
        left = left.strip()
        if left in entries:
            errors.append("api_map.txt:%d: duplicate entry %s" % (no, left))
        entries[left] = (no, right.strip())
    return entries, extras, errors


def cpp_names(text):
    """C++ names written as PAKLib::a::b or a::b(...) / a(...) on the right-hand side."""
    names = re.findall(r"\b((?:[a-z_][a-z0-9_]*::)*[a-z_][a-z0-9_]*)\s*\(", text)
    names += re.findall(r"\b((?:[a-z_][a-z0-9_]*::)+[a-z_][a-z0-9_]*)\b", text)
    return sorted(set(n.replace("PAKLib::", "") for n in names if n not in ("std",)))


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--paklib", default=os.path.normpath(os.path.join(HERE, "..", "..", "..", "PAKLib", "PAKLib")))
    p.add_argument("--header", default=os.path.normpath(os.path.join(HERE, "..", "..", "PAKLibC", "PAK.h")))
    p.add_argument("--map", default=os.path.join(HERE, "api_map.txt"))
    args = p.parse_args()
    if not os.path.isdir(args.paklib):
        print("PAKLib sources not found: " + args.paklib)
        return 2
    public = scrape(args.paklib)
    entries, extras, errors = read_map(args.map)
    header = open(args.header, encoding="utf-8").read()
    header_names = set(re.findall(r"\b[a-z_][a-z0-9_]*\b", header))

    unmapped = sorted(public - set(entries))
    stale = sorted(set(entries) - public)
    dangling = []
    for left, (no, right) in sorted(entries.items(), key=lambda kv: kv[1][0]):
        if right.startswith("(none)"):
            continue
        for name in cpp_names(right):
            if name.split("::")[-1] not in header_names:
                dangling.append("api_map.txt:%d: %s names %s, which PAK.h does not declare" % (no, left, name))
    for no, text in extras:
        for name in cpp_names(text) or [text.split()[0]]:
            if name.split("::")[-1] not in header_names:
                dangling.append("api_map.txt:%d: extra %s is not in PAK.h" % (no, name))

    print("PAKLib public API: %d types and members scraped from %s" % (len(public), os.path.basename(args.paklib.rstrip("\\/"))))
    print("api_map.txt: %d mapped, %d extras" % (len(entries), len(extras)))
    for e in errors:
        print("ERROR " + e)
    for k in unmapped:
        print("UNMAPPED " + k)
    for k in stale:
        print("STALE " + k + " (api_map.txt:%d)" % entries[k][0])
    for d in dangling:
        print("DANGLING " + d)
    ok = not (errors or unmapped or stale or dangling)
    print("check_api: " + ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
