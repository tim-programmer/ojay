#!/usr/bin/env python3
"""Checks the C calling convention of ojay's #c_call / #foreign procedures
against clang's.

For each case below, the same type is written in C and in Jai. The script
compiles C prototypes with clang and Jai declarations (`#foreign`) and
definitions (`#c_call #program_export`) with each ojay compiler to LLVM IR,
then compares the function signatures: the coerced parameter and result types,
how they are split into IR arguments, and the byval / sret / align /
signext / zeroext attributes. Named struct types are compared by their
flattened scalar contents, so clang's `%struct.F3` and ojay's `%F3` match.

    tools/check_c_abi.py [--build-dir build] [-v]

This is a development check, separate from the compilers; the run tests
(tests/run/c_abi*.jai with their .c files) check the same lowering by
executing it.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

TARGETS = {
    "x86_64": ("ojay-x86_64", ["--target=x86_64-pc-linux-gnu"]),
    "arm32": ("ojay-arm32", ["--target=thumbv8m.main-none-eabihf", "-mcpu=cortex-m33", "-mfloat-abi=hard",
                             "-mfpu=fpv5-sp-d16"]),
    "riscv": ("ojay-riscv", ["--target=riscv32-unknown-none-elf", "-march=rv32imac", "-mabi=ilp32"]),
}

# (name, C members, Jai members). Each gets `T f_T(T x)` and a callback `g_T`.
TYPES = [
    ("S1", "int8_t a;", "a: s8;"),
    ("S2", "int16_t a;", "a: s16;"),
    ("S3", "int8_t a, b, c;", "a, b, c: s8;"),
    ("S4", "int32_t a;", "a: s32;"),
    ("S8", "int32_t a, b;", "a, b: s32;"),
    ("S12", "int32_t a, b, c;", "a, b, c: s32;"),
    ("S16", "int32_t a, b, c, d;", "a, b, c, d: s32;"),
    ("S24", "int64_t a, b, c;", "a, b, c: s64;"),
    ("S64", "int32_t a[16];", "a: [16]s32;"),
    ("S72", "int32_t a[18];", "a: [18]s32;"),
    ("L", "int64_t a;", "a: s64;"),
    ("HB", "int16_t a; int8_t b;", "a: s16; b: s8;"),
    ("A5", "uint8_t a[5];", "a: [5]u8;"),
    ("P", "void *p;", "p: *void;"),
    ("PI", "void *p; int32_t i;", "p: *void; i: s32;"),
    ("BB", "bool a, b;", "a, b: bool;"),
    ("EN", "uint8_t e; int16_t f;", "e: En8; f: En16;"),
    ("FI", "float f; int32_t i;", "f: float32; i: s32;"),
    ("CF", "int8_t x; float f;", "x: s8; f: float32;"),
    ("DD", "double a, b;", "a, b: float64;"),
    ("F1", "float a;", "a: float32;"),
    ("F2", "float a, b;", "a, b: float32;"),
    ("F3", "float a, b, c;", "a, b, c: float32;"),
    ("F4", "float a, b, c, d;", "a, b, c, d: float32;"),
    ("F5", "float a, b, c, d, e;", "a, b, c, d, e: float32;"),
    ("D1", "double a;", "a: float64;"),
    ("D3", "double a, b, c;", "a, b, c: float64;"),
    ("D4", "double a, b, c, d;", "a, b, c, d: float64;"),
    ("ID", "int8_t a; double d;", "a: s8; d: float64;"),
    ("FD", "float a; double b;", "a: float32; b: float64;"),
    ("F4A", "float a[4];", "a: [4]float32;"),
    ("FAD", "float a[2]; double d;", "a: [2]float32; d: float64;"),
    ("DAF", "double a[2]; float f;", "a: [2]float64; f: float32;"),
    ("NF", "F2 v; float z;", "v: F2; z: float32;"),
    ("NI", "S3 s; int8_t t; S8 u;", "s: S3; t: s8; u: S8;"),
    ("E", "", ""),
    ("EI", "E e; int32_t a;", "e: E; a: s32;"),
    ("FEF", "float a; E e; float b;", "a: float32; e: E; b: float32;"),
    ("SU", "int32_t a; UFI u;", "a: s32; u: UFI;"),
    ("SUF", "float a; UFF u;", "a: float32; u: UFF;"),
    ("STR", "int64_t count; uint8_t *data;", "s: string;"),
]
UNIONS = [
    ("UFI", "float f; int32_t i;", "f: float32; i: s32;"),
    ("UFF", "float f; float g[2];", "f: float32; g: [2]float32;"),
    ("UFD", "float f; double d;", "f: float32; d: float64;"),
    ("UIL", "int8_t c; int64_t l;", "c: s8; l: s64;"),
]

# Extra prototypes: (C, Jai). Structs after enough scalars to run out of registers.
EXTRA = [
    ("void x_spill_int(int a, int b, int c, int d, int e, S16 s);",
     "x_spill_int :: (a: s32, b: s32, c: s32, d: s32, e: s32, s: S16) #foreign;"),
    ("void x_spill_int6(int a, int b, int c, int d, int e, int f, S8 s, S8 t);",
     "x_spill_int6 :: (a: s32, b: s32, c: s32, d: s32, e: s32, f: s32, s: S8, t: S8) #foreign;"),
    ("void x_spill_sse(double a, double b, double c, double d, double e, double f, double g, double h, DD s, F1 t);",
     "x_spill_sse :: (a: float64, b: float64, c: float64, d: float64, e: float64, f: float64, g: float64, "
     "h: float64, s: DD, t: F1) #foreign;"),
    ("S24 x_sret_spill(int a, int b, int c, int d, int e, S16 s);",
     "x_sret_spill :: (a: s32, b: s32, c: s32, d: s32, e: s32, s: S16) -> S24 #foreign;"),
    ("void x_hfa_spill(F4 a, F4 b, F4 c, F4 d, F4 e);",
     "x_hfa_spill :: (a: F4, b: F4, c: F4, d: F4, e: F4) #foreign;"),
    ("void x_mixed(int8_t a, S3 b, float c, DD d, uint16_t e, ID f, bool g);",
     "x_mixed :: (a: s8, b: S3, c: float32, d: DD, e: u16, f: ID, g: bool) #foreign;"),
    ("S1 x_noargs(void);", "x_noargs :: () -> S1 #foreign;"),
    ("void x_empty(E a, int b, E c);", "x_empty :: (a: E, b: s32, c: E) #foreign;"),
]


def sources():
    c = ["#include <stdint.h>", "#include <stdbool.h>"]
    jai = ["En8 :: enum u8 { A; B; }", "En16 :: enum s16 { A :: -1; B; }"]
    order = TYPES[:]
    # C needs definitions before use; the unions come before SU and SUF.
    for name, cm, jm in UNIONS:
        c.append(f"typedef union {{ {cm} }} {name};")
        jai.append(f"{name} :: union {{ {jm} }}")
    for name, cm, jm in order:
        c.append(f"typedef struct {{ {cm} }} {name};")
        jai.append(f"{name} :: struct {{ {jm} }}")
    names = [n for n, _, _ in UNIONS] + [n for n, _, _ in order]
    refs = []
    for n in names:
        c.append(f"{n} f_{n}({n} x) {{ return x; }}")
        jai.append(f"f_{n} :: (x: {n}) -> {n} #foreign;")
        jai.append(f"g_{n} :: (x: {n}) -> {n} #c_call #program_export {{ return x; }}")
        refs.append(f"f_{n}")
    for cp, jp in EXTRA:
        # clang only emits prototypes that are used; definitions it always emits.
        rt = cp.split(" ")[0]
        c.append(cp[:-1] + (" {}" if rt == "void" else f" {{ {rt} r = {{0}}; return r; }}"))
        jai.append(jp)
        refs.append(jp.split(" ")[0])
    jai.append("main :: () {")
    for r in refs:
        jai.append(f"    {r}_ref := {r};")
    jai.append("}")
    return "\n".join(c) + "\n", "\n".join(jai) + "\n", names


# ---- LLVM IR signature parsing

def split_top(s, sep=","):
    out, depth, cur = [], 0, ""
    for ch in s:
        if ch in "([{<":
            depth += 1
        elif ch in ")]}>":
            depth -= 1
        if ch == sep and depth == 0:
            out.append(cur)
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur)
    return [x.strip() for x in out]


def parse_type(s, i=0):
    """Returns (type tree, next index). Trees: str scalar, ('struct', [..]), ('array', n, t),
    ('vector', n, t), ('named', name)."""
    while s[i] == " ":
        i += 1
    if s[i] == "{":
        i += 1
        elems = []
        while True:
            while s[i] == " ":
                i += 1
            if s[i] == "}":
                return ("struct", elems), i + 1
            t, i = parse_type(s, i)
            elems.append(t)
            while s[i] == " ":
                i += 1
            if s[i] == ",":
                i += 1
    if s[i] in "[<":
        close = "]" if s[i] == "[" else ">"
        m = re.match(r"[\[<](\d+) x ", s[i:])
        i += m.end()
        t, i = parse_type(s, i)
        while s[i] == " ":
            i += 1
        assert s[i] == close
        return ("array" if close == "]" else "vector", int(m.group(1)), t), i + 1
    m = re.match(r"%[\w.$\"()]+|i\d+|float|double|ptr|void|half", s[i:])
    tok = m.group(0)
    if tok.startswith("%"):
        return ("named", tok), i + len(tok)
    return tok, i + len(tok)


def flatten(t, types):
    if isinstance(t, str):
        return [t]
    if t[0] == "named":
        return flatten(types[t[1]], types)
    if t[0] == "struct":
        return [x for e in t[1] for x in flatten(e, types)]
    if t[0] in ("array", "vector"):
        return flatten(t[2], types) * t[1]
    raise ValueError(t)


def show(t, types):
    if isinstance(t, str):
        return t
    if t[0] in ("named", "struct"):  # by scalar contents: clang's %struct.X and a literal struct compare equal
        return "{" + ", ".join(flatten(t, types)) + "}"
    if t[0] == "array":
        return f"[{t[1]} x {show(t[2], types)}]"
    return f"<{t[1]} x {show(t[2], types)}>"


def norm_part(part, types):
    """One parameter (or the result with its attributes) -> normalized string."""
    t, i = parse_type(part)
    rest = part[i:]
    attrs = []
    for a in ("byval", "sret"):
        if re.search(r"\b" + a + r"\(", rest):
            attrs.append(a)
    for a in ("zeroext", "signext"):
        if re.search(r"\b" + a + r"\b", rest):
            attrs.append(a)
    m = re.search(r"\balign (\d+)", rest)
    if m and t == "ptr":
        attrs.append("align " + m.group(1))
    return " ".join([show(t, types)] + attrs)


def signatures(ir):
    types = {}
    for m in re.finditer(r"^(%[\w.$\"]+) = type (.*)$", ir, re.M):
        types[m.group(1)] = parse_type(m.group(2))[0]
    sigs = {}
    for m in re.finditer(r"^(?:declare|define)\s+(.*?)@([\w.]+)\((.*)\)[^)]*$", ir, re.M):
        head, name, params = m.group(1), m.group(2), m.group(3)
        # Result: drop linkage and attributes in front of the type.
        head = re.sub(r"\b(dso_local|internal|noundef|range\([^)]*\)|nonnull|local_unnamed_addr)\b", "", head)
        ret_ext = [a for a in ("zeroext", "signext") if re.search(r"\b" + a + r"\b", head)]
        head = re.sub(r"\b(zeroext|signext)\b", "", head).strip()
        ret = norm_part(head, types) + "".join(" " + a for a in ret_ext)
        # Parameters: the attribute order differs; put the type first.
        ps = []
        for p in split_top(params):
            p = re.sub(r"%\d+$", "", p).strip()
            ps.append(norm_part(p, types))
        sigs[name] = ret + " (" + ", ".join(ps) + ")"
    return sigs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build-dir", default=os.path.join(REPO, "build"))
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()
    clang = shutil.which("clang")
    if not clang:
        print("clang not found")
        return 2
    c_src, jai_src, names = sources()
    tmp = tempfile.mkdtemp(prefix="ojay-abi-")
    with open(os.path.join(tmp, "abi.c"), "w") as f:
        f.write(c_src)
    with open(os.path.join(tmp, "abi.jai"), "w") as f:
        f.write(jai_src)
    failures = checked = 0
    for target, (compiler, cflags) in TARGETS.items():
        r = subprocess.run([clang] + cflags + ["-O0", "-S", "-emit-llvm", "-o", "-", os.path.join(tmp, "abi.c")],
                           capture_output=True, text=True)
        if r.returncode:
            print(r.stderr)
            return 2
        want = signatures(r.stdout)
        r = subprocess.run([os.path.join(args.build_dir, "bin", compiler), "--emit-llvm", "-o", "-",
                            os.path.join(tmp, "abi.jai")], capture_output=True, text=True)
        if r.returncode:
            print(r.stderr)
            return 2
        got = signatures(r.stdout)
        pairs = [(f"f_{n}", f"f_{n}") for n in names] + [(f"g_{n}", f"f_{n}") for n in names]
        pairs += [(jp.split(" ")[0], jp.split(" ")[0]) for _, jp in EXTRA]
        for mine, ref in pairs:
            checked += 1
            g, w = got.get(mine), want.get(ref)
            if g != w:
                failures += 1
                print(f"MISMATCH [{target}] {mine}\n   ojay:  {g}\n   clang: {w}")
            elif args.verbose:
                print(f"ok [{target}] {mine}: {g}")
    shutil.rmtree(tmp, ignore_errors=True)
    print(f"{checked - failures} signatures match clang, {failures} differ")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
