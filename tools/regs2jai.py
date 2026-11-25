#!/usr/bin/env python3
"""Converts a CMSIS-SVD file or a CMSIS device header into Jai code that an
ojay program can #load (or #import, with -I) to access memory-mapped registers.

    regs2jai.py MCU.svd -o mcu.jai
    regs2jai.py MCXN947_cm33_core0.h -o mcxn947.jai -- -I path/to/CMSIS/Core/Include

The output follows the CMSIS conventions, so C code ports over directly:

    GPIO_Type :: struct { ... PDOR: u32; ... }    // one struct per register block
    GPIO0_BASE :: 0x4009_6000;
    GPIO0 :: cast(*GPIO_Type) GPIO0_BASE;
    GPIO_PDOR_PDO0_MASK  :: 0x1;                  // register fields
    GPIO_PDOR_PDO0_SHIFT :: 0;
    GPIO_PDOR_PDO0 :: inline (x: u32) -> u32 { ... }   // like the C macro GPIO_PDOR_PDO0(x)

and registers are accessed with the volatile intrinsics of ojay's "Hardware"
module (which the generated file imports):

    volatile_store(*GPIO0.PSOR, GPIO_PDOR_PDO0(1));
    x := volatile_load(*GPIO0.PDIR);

SVD input: registers are placed at their offsets (gaps become `_reserved`
arrays, registers sharing an address become anonymous unions), register and
cluster arrays become Jai arrays, enumerated field values become constants
(<TYPE>_<REG>_<FIELD>_<VALUE>), and interrupts become <NAME>_IRQn constants.

Header input: the header is run through the C preprocessor (clang -E -dD, or
--cc), so #if/#include are resolved exactly as for C code; pass compiler
options (-I, -D, --target) after `--`. Struct typedefs, enums and integer and
peripheral-pointer #defines are translated; anything else (inline functions,
bitfield structs, macros that are not constants) is skipped. By default only
declarations from the input file and the files next to it or in other
non-system directories are kept; --include/--exclude select files by regex.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET

JAI_RESERVED = set("""
if else then ifx case while for break continue return defer struct union enum enum_flags using cast xx
true false null inline remove it it_index
int float float32 float64 bool string void Type Any u8 u16 u32 u64 s8 s16 s32 s64
""".split())

WARNINGS = []


def warn(msg):
    WARNINGS.append(msg)
    print("regs2jai: warning: " + msg, file=sys.stderr)


def ident(name):
    """A valid Jai identifier for `name`."""
    s = re.sub(r"[^A-Za-z0-9_]", "_", name.strip())
    if not s or s[0].isdigit():
        s = "_" + s
    if s in JAI_RESERVED:
        s += "_"
    return s


def int_literal(v, hex_style=True):
    if v < 0 or not hex_style or v < 10:
        return str(v)
    h = "%X" % v
    if len(h) > 4:  # group into 4-digit chunks: 0x4009_6000
        h = "_".join(h[max(0, i - 4):i] for i in range(len(h), 0, -4)[::-1])
    return "0x" + h


def one_line(text, limit=100):
    if not text:
        return ""
    s = " ".join(text.split())
    return s if len(s) <= limit else s[:limit - 3].rstrip() + "..."


# ---------------------------------------------------------------------------
# Jai type model: what the emitter prints.

class Prim:
    def __init__(self, name, size):
        self.name, self.size, self.align = name, size, size


class Ptr:
    def __init__(self, target, size):
        self.target, self.size, self.align = target, size, size


class Arr:
    def __init__(self, elem, count):
        self.elem, self.count = elem, count
        self.size, self.align = elem.size * count, elem.align


class Named:
    """A reference to a struct emitted at top level."""
    def __init__(self, name, rec):
        self.name, self.rec = name, rec
        self.size, self.align = rec.size, rec.align


class Field:
    def __init__(self, name, type_, comment=""):
        self.name, self.type, self.comment = name, type_, comment


class Rec:
    """struct or union; fields named None are anonymous struct/union members."""
    def __init__(self, fields, is_union=False):
        self.fields, self.is_union = fields, is_union
        self.align = max([f.type.align for f in fields] or [1])
        if is_union:
            size = max([f.type.size for f in fields] or [0])
        else:
            size = 0
            for f in fields:
                size = (size + f.type.align - 1) // f.type.align * f.type.align + f.type.size
        self.size = (size + self.align - 1) // self.align * self.align


PRIMS = {n: Prim(n, s) for n, s in [("u8", 1), ("u16", 2), ("u32", 4), ("u64", 8), ("s8", 1), ("s16", 2),
                                    ("s32", 4), ("s64", 8), ("float32", 4), ("float64", 8), ("bool", 1)]}
UNSIGNED_BY_SIZE = {1: PRIMS["u8"], 2: PRIMS["u16"], 4: PRIMS["u32"], 8: PRIMS["u64"]}


def type_text(t, indent):
    if isinstance(t, Prim):
        return t.name
    if isinstance(t, Named):
        return t.name
    if isinstance(t, Ptr):
        return "*" + ("void" if t.target is None else type_text(t.target, indent))
    if isinstance(t, Arr):
        return "[%d]%s" % (t.count, type_text(t.elem, indent))
    return rec_text(t, indent)


def rec_text(rec, indent):
    pad = "    " * (indent + 1)
    lines = ["union {" if rec.is_union else "struct {"]
    for f in rec.fields:
        comment = ("  // " + f.comment.rstrip()) if f.comment else ""
        if f.name is None:
            lines.append(pad + type_text(f.type, indent + 1) + comment)
        else:
            lines.append("%s%s: %s;%s" % (pad, f.name, type_text(f.type, indent + 1), comment))
    lines.append("    " * indent + "}")
    return "\n".join(lines)


class Output:
    """Collects top-level declarations, refusing duplicate names."""
    def __init__(self):
        self.parts = []
        self.names = set()

    def claim(self, name, what):
        if name in self.names:
            warn("skipping %s '%s': the name is already defined" % (what, name))
            return False
        self.names.add(name)
        return True

    def raw(self, text):
        self.parts.append(text)

    def struct(self, name, rec, comment=""):
        if not self.claim(name, "struct"):
            return
        if comment:
            self.parts.append("// " + comment)
        self.parts.append("%s :: %s\n#assert size_of(%s) == %s;" % (name, rec_text(rec, 0), name,
                                                                   int_literal(rec.size)))

    def const(self, name, value_text, comment="", align=0):
        if not self.claim(name, "constant"):
            return
        line = "%-*s :: %s;" % (align, name, value_text)
        if comment:
            line += "  // " + comment
        self.parts.append(line)

    def field_proc(self, name, typ, shift, mask):
        if not self.claim(name, "procedure"):
            return
        self.parts.append("%s :: inline (x: %s) -> %s { return (x << %s) & %s; }" % (name, typ, typ, shift, mask))

    def text(self, header):
        out, prev_blank = [header], False
        for p in self.parts:
            blank = p == ""
            if not (blank and prev_blank):
                out.append(p)
            prev_blank = blank
        return "\n".join(out).rstrip() + "\n"


# ---------------------------------------------------------------------------
# Laying out registers given by offset (SVD).

class Placed:
    def __init__(self, offset, field):
        self.offset, self.field = offset, field

    @property
    def end(self):
        return self.offset + self.field.type.size


def reserved_name(counter):
    counter[0] += 1
    return "_reserved%d" % (counter[0] - 1)


def layout(items, total_size=None, counter=None):
    """Turns registers at byte offsets into a Rec: gaps become reserved byte
    arrays, overlapping registers become an anonymous union whose
    alternatives are anonymous structs."""
    counter = counter if counter is not None else [0]
    items = sorted(items, key=lambda p: p.offset)
    fields, cursor, i = [], 0, 0
    while i < len(items):
        group, end = [items[i]], items[i].end
        i += 1
        while i < len(items) and items[i].offset < end:
            group.append(items[i])
            end = max(end, items[i].end)
            i += 1
        start = group[0].offset
        if start > cursor:
            fields.append(Field(reserved_name(counter), Arr(PRIMS["u8"], start - cursor)))
            cursor = start
        if len(group) == 1:
            f = group[0].field
            if start % f.type.align:
                warn("register '%s' at offset 0x%X is not aligned to its size; emitted as bytes" % (f.name, start))
                f = Field(f.name, Arr(PRIMS["u8"], f.type.size), f.comment)
            fields.append(f)
        else:
            alts = []  # each alternative: registers that do not overlap one another
            for p in group:
                for alt in alts:
                    if alt[-1].end <= p.offset:
                        alt.append(p)
                        break
                else:
                    alts.append([p])
            members = []
            for alt in alts:
                if len(alt) == 1 and alt[0].offset == start:
                    members.append(alt[0].field)
                else:
                    members.append(Field(None, layout([Placed(p.offset - start, p.field) for p in alt],
                                                      None, counter)))
            fields.append(Field(None, Rec(members, is_union=True)))
        cursor = start + Rec([fields[-1]]).size if fields[-1].name is None else end
        cursor = max(cursor, end)
    rec = Rec(fields)
    if total_size is not None and total_size > rec.size:
        fields.append(Field(reserved_name(counter), Arr(PRIMS["u8"], total_size - rec.size)))
        rec = Rec(fields)
    return rec


# ---------------------------------------------------------------------------
# SVD

def svd_int(text, default=None):
    if text is None:
        return default
    s = text.strip().lower()
    if not s:
        return default
    mult = 1
    if s[-1] in "km":
        mult = 1024 if s[-1] == "k" else 1024 * 1024
        s = s[:-1]
    if s.startswith("#"):
        if "x" in s:
            return None  # "don't care" bits in an enumerated value
        return int(s[1:], 2) * mult
    if s.startswith("0b"):
        if "x" in s[2:]:
            return None
        return int(s[2:], 2) * mult
    if s.startswith("0x"):
        return int(s, 16) * mult
    return int(s, 10) * mult  # decimal, even with leading zeros


def child_text(el, tag):
    c = el.find(tag)
    return c.text.strip() if c is not None and c.text is not None else None


def dim_names(el):
    """For an element with <dim>: the list of index strings, else None."""
    dim = svd_int(child_text(el, "dim"))
    if dim is None:
        return None
    idx = child_text(el, "dimIndex")
    if idx:
        m = re.fullmatch(r"(\d+)\s*-\s*(\d+)", idx)
        if m:
            names = [str(i) for i in range(int(m.group(1)), int(m.group(2)) + 1)]
        else:
            m = re.fullmatch(r"([A-Za-z])\s*-\s*([A-Za-z])", idx)
            if m:
                names = [chr(c) for c in range(ord(m.group(1)), ord(m.group(2)) + 1)]
            else:
                names = [s.strip() for s in idx.split(",")]
        if len(names) == dim:
            return names
        warn("dimIndex '%s' does not have %d entries" % (idx, dim))
    return [str(i) for i in range(dim)]


class SvdConverter:
    def __init__(self, path, args):
        self.args = args
        self.tree = ET.parse(path)
        self.dev = self.tree.getroot()
        self.out = Output()

    def rec_signature(self, rec):
        return rec_text(rec, 0)

    def convert(self):
        dev = self.dev
        default_size = svd_int(child_text(dev, "size"), 32)
        name = child_text(dev, "name") or "device"
        periphs = dev.findall("peripherals/peripheral")
        by_name = {child_text(p, "name"): p for p in periphs}
        o = self.out
        irqs = {}
        # Pass 1: the register block layout of every peripheral.
        entries = []  # (peripheral, element with its registers, signature or None)
        blocks = {}   # signature -> (element, Rec, field constants)
        for p in periphs:
            pname = child_text(p, "name")
            base_el = p
            derived = p.get("derivedFrom")
            if derived and p.find("registers") is None:
                base_el = by_name.get(derived)
                if base_el is None:
                    warn("peripheral %s derives from unknown %s" % (pname, derived))
                    continue
            sig = None
            regs = base_el.find("registers")
            if regs is not None:
                size = svd_int(child_text(p, "size"), svd_int(child_text(base_el, "size"), default_size))
                consts = []
                rec = layout(self.block_items(regs, size, consts, [], 0))
                sig = self.rec_signature(rec)
                blocks.setdefault(sig, (base_el, rec, consts))
            entries.append((p, base_el, sig))
            for irq in p.findall("interrupt"):
                irqs[child_text(irq, "name")] = (svd_int(child_text(irq, "value")),
                                                 one_line(child_text(irq, "description")))
        # Pass 2: one struct per distinct layout. It is named after the header
        # struct name or group name when every peripheral using that name has
        # this layout, else after the first peripheral with it.
        layouts_of = {}
        for p, base_el, sig in entries:
            if sig is not None:
                for tag in ("headerStructName", "groupName"):
                    if child_text(base_el, tag):
                        layouts_of.setdefault((tag, child_text(base_el, tag)), set()).add(sig)
        type_names = {}
        for p, base_el, sig in entries:
            if sig is None or sig in type_names:
                continue
            cands = [child_text(base_el, t) for t in ("headerStructName", "groupName")
                     if child_text(base_el, t) and layouts_of[(t, child_text(base_el, t))] == {sig}]
            cands += [child_text(base_el, "name"), child_text(p, "name")]
            taken = set(type_names.values())
            prefix = next((ident(c) for c in cands if ident(c) + "_Type" not in taken), None)
            n = 2
            while prefix is None:
                if ident(cands[-1]) + "_%d_Type" % n not in taken:
                    prefix = ident(cands[-1]) + "_%d" % n
                n += 1
            type_names[sig] = prefix + "_Type"
            el, rec, consts = blocks[sig]
            o.raw("")
            o.raw("// ---- %s" % (one_line(child_text(el, "description")) or prefix))
            o.raw("")
            o.struct(prefix + "_Type", rec)
            self.emit_fields(prefix, consts)
        instances = []
        for p, base_el, sig in entries:
            desc = one_line(child_text(p, "description") or child_text(base_el, "description"))
            instances.append((ident(child_text(p, "name")), svd_int(child_text(p, "baseAddress")),
                              type_names.get(sig), desc))

        o.raw("")
        o.raw("// ---- Peripheral instances")
        for pname, base, type_name, desc in instances:
            o.raw("")
            if desc:
                o.raw("// " + desc)
            o.const(pname + "_BASE", int_literal(base))
            if type_name:
                o.const(pname, "cast(*%s) %s_BASE" % (type_name, pname))
        if irqs:
            o.raw("")
            o.raw("// ---- Interrupt numbers")
            width = max(len(ident(n) + "_IRQn") for n in irqs)
            for n, (v, desc) in sorted(irqs.items(), key=lambda kv: kv[1][0]):
                o.const(ident(n) + "_IRQn", str(v), desc, width)
        header = ("// Generated by ojay tools/regs2jai.py from %s (device %s). Do not edit.\n\n"
                  "#import \"Hardware\";   // volatile_load / volatile_store\n"
                  % (os.path.basename(self.args.input), name))
        return o.text(header)

    def block_items(self, parent, default_size, consts, path, base_offset):
        """Registers and clusters under `parent` as Placed items."""
        items = []
        by_name = {}
        for child in parent:
            if child.tag in ("register", "cluster"):
                by_name[child_text(child, "name")] = child
        for child in parent:
            if child.tag not in ("register", "cluster"):
                continue
            el = child
            if child.get("derivedFrom") and child.get("derivedFrom") in by_name:
                el = merge_derived(by_name[child.get("derivedFrom")], child)
            name = child_text(el, "name")
            offset = svd_int(child_text(el, "addressOffset"), 0)
            names = dim_names(el)
            inc = svd_int(child_text(el, "dimIncrement"), 0)
            desc = one_line(child_text(el, "description"))
            if child.tag == "register":
                size = svd_int(child_text(el, "size"), default_size)
                typ = UNSIGNED_BY_SIZE.get(size // 8)
                if typ is None or size % 8:
                    warn("register %s has unsupported size %d" % (name, size))
                    continue
                fields_el = el.find("fields")
                if names is None:
                    items.append(Placed(offset, Field(ident(name), typ, "0x%03X %s" % (offset, desc))))
                    self.collect_fields(path + [name], typ, fields_el, consts)
                elif "[%s]" in name and inc == typ.size:
                    base = name.replace("[%s]", "")
                    items.append(Placed(offset, Field(ident(base), Arr(typ, len(names)),
                                                      "0x%03X %s" % (offset, desc))))
                    self.collect_fields(path + [base], typ, fields_el, consts)
                else:
                    stem = name.replace("[%s]", "%s")
                    for i, idx in enumerate(names):
                        n = stem.replace("%s", idx)
                        items.append(Placed(offset + i * inc, Field(ident(n), typ, "0x%03X %s" % (
                            offset + i * inc, desc))))
                    self.collect_fields(path + [stem.replace("%s", "")], typ, fields_el, consts)
            else:  # cluster
                size = svd_int(child_text(el, "size"), default_size)
                cname = name.replace("[%s]", "").replace("%s", "")
                inner = self.block_items(el, size, consts, path + [cname], 0)
                if names is None:
                    rec = layout(inner)
                    items.append(Placed(offset, Field(ident(name), rec, "0x%03X %s" % (offset, desc))))
                elif inc >= layout(inner).size:
                    rec = layout(inner, inc)
                    if "[%s]" in name:
                        items.append(Placed(offset, Field(ident(cname), Arr(rec, len(names)),
                                                          "0x%03X %s" % (offset, desc))))
                    else:
                        for i, idx in enumerate(names):
                            items.append(Placed(offset + i * inc, Field(ident(name.replace("%s", idx)), rec)))
                else:
                    warn("cluster %s: elements overlap (dimIncrement 0x%X); expanded" % (name, inc))
                    rec = layout(inner)
                    for i, idx in enumerate(names):
                        n = name.replace("[%s]", idx).replace("%s", idx)
                        items.append(Placed(offset + i * inc, Field(ident(n), rec)))
        return items

    def collect_fields(self, path, typ, fields_el, consts):
        if fields_el is None:
            return
        for f in fields_el.findall("field"):
            fname = child_text(f, "name")
            if child_text(f, "bitOffset") is not None:
                lsb = svd_int(child_text(f, "bitOffset"))
                width = svd_int(child_text(f, "bitWidth"), 1)
            elif child_text(f, "lsb") is not None:
                lsb = svd_int(child_text(f, "lsb"))
                width = svd_int(child_text(f, "msb")) - lsb + 1
            else:
                m = re.fullmatch(r"\[\s*(\d+)\s*:\s*(\d+)\s*\]", child_text(f, "bitRange") or "")
                if not m:
                    warn("field %s has no bit position" % fname)
                    continue
                lsb, width = int(m.group(2)), int(m.group(1)) - int(m.group(2)) + 1
            names = dim_names(f)
            inc = svd_int(child_text(f, "dimIncrement"), width)
            variants = [(fname, lsb)] if names is None else [
                (fname.replace("[%s]", idx).replace("%s", idx), lsb + i * inc) for i, idx in enumerate(names)]
            enums = []
            if not self.args.no_enums:
                for ev in f.findall("enumeratedValues/enumeratedValue"):
                    v = svd_int(child_text(ev, "value"))
                    if v is None or child_text(ev, "name") is None:
                        continue
                    enums.append((child_text(ev, "name"), v, one_line(child_text(ev, "description"), 80)))
            desc = one_line(child_text(f, "description"), 80)
            for n, bit in variants:
                consts.append(("_".join(ident(x) for x in path + [n]), typ, bit, width, desc, enums))

    def emit_fields(self, prefix, consts):
        o = self.out
        for name, typ, lsb, width, desc, enums in consts:
            full = prefix + "_" + name
            mask = ((1 << width) - 1) << lsb
            o.raw("")
            o.const(full + "_MASK", int_literal(mask), desc)
            o.const(full + "_SHIFT", str(lsb))
            if not self.args.no_field_procs:
                o.field_proc(full, typ.name, full + "_SHIFT", full + "_MASK")
            for en, v, edesc in enums:
                o.const(full + "_" + ident(en), int_literal(v, v > 9), edesc)


def merge_derived(base, child):
    """A register/cluster `derivedFrom` another: the base with the child's elements replacing its own."""
    import copy
    el = copy.deepcopy(base)
    for c in child:
        old = el.find(c.tag)
        if old is not None:
            el.remove(old)
        el.append(copy.deepcopy(c))
    return el


# ---------------------------------------------------------------------------
# CMSIS headers

C_TYPES = {
    "uint8_t": "u8", "uint16_t": "u16", "uint32_t": "u32", "uint64_t": "u64",
    "int8_t": "s8", "int16_t": "s16", "int32_t": "s32", "int64_t": "s64",
    "char": "u8", "signed char": "s8", "unsigned char": "u8",
    "short": "s16", "short int": "s16", "signed short": "s16", "unsigned short": "u16",
    "unsigned short int": "u16", "int": "s32", "signed int": "s32", "signed": "s32", "unsigned": "u32",
    "unsigned int": "u32", "long long": "s64", "long long int": "s64", "unsigned long long": "u64",
    "unsigned long long int": "u64", "float": "float32", "double": "float64", "_Bool": "bool", "bool": "bool",
}
QUALIFIERS = {"volatile", "const", "static", "register", "restrict", "__restrict", "__volatile__"}

TOKEN_RE = re.compile(r"""
    (?P<ws>\s+)
  | (?P<num>0[xX][0-9A-Fa-f]+[uUlL]*|0[bB][01]+[uUlL]*|\d+[uUlL]*)
  | (?P<id>[A-Za-z_]\w*)
  | (?P<str>"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')
  | (?P<op><<|>>|<=|>=|==|!=|&&|\|\||->|\.\.\.|[-+*/%&|^~!<>?:;,.(){}\[\]=#])
""", re.X)


def c_tokens(text):
    toks, pos = [], 0
    while pos < len(text):
        m = TOKEN_RE.match(text, pos)
        if not m:
            toks.append(text[pos])
            pos += 1
            continue
        pos = m.end()
        if m.lastgroup != "ws":
            toks.append(m.group())
    return toks


class CError(Exception):
    pass


def c_number(tok):
    t = tok.rstrip("uUlL")
    if t[:2] in ("0b", "0B"):
        return int(t[2:], 2)
    if t[:2] in ("0x", "0X"):
        return int(t, 16)
    if len(t) > 1 and t[0] == "0":
        return int(t, 8)
    return int(t)


class ConstEval:
    """Evaluates C integer constant expressions, expanding object-like macros."""
    INT_CASTS = {"uint8_t": (8, False), "uint16_t": (16, False), "uint32_t": (32, False), "uint64_t": (64, False),
                 "int8_t": (8, True), "int16_t": (16, True), "int32_t": (32, True), "int64_t": (64, True),
                 "unsigned": (32, False), "int": (32, True), "signed": (32, True), "char": (8, False),
                 "long": (32, True), "short": (16, True)}

    def __init__(self, macros, enums):
        self.macros, self.enums, self.active = macros, enums, set()

    def value(self, name):
        if name in self.enums:
            return self.enums[name]
        m = self.macros.get(name)
        if m is None or m.params is not None or name in self.active:
            raise CError("not a constant: " + name)
        self.active.add(name)
        try:
            return self.eval(m.tokens)
        finally:
            self.active.discard(name)

    def eval(self, toks):
        saved = (getattr(self, "toks", None), getattr(self, "pos", 0))  # eval re-enters through value()
        self.toks, self.pos = list(toks), 0
        try:
            if not self.toks:
                raise CError("empty")
            v = self.ternary()
            if self.pos != len(self.toks):
                raise CError("trailing tokens")
            return v
        finally:
            self.toks, self.pos = saved

    def peek(self):
        return self.toks[self.pos] if self.pos < len(self.toks) else None

    def take(self):
        t = self.peek()
        if t is None:
            raise CError("unexpected end")
        self.pos += 1
        return t

    def ternary(self):
        c = self.binary(0)
        if self.peek() == "?":
            self.take()
            a = self.ternary()
            if self.take() != ":":
                raise CError("expected ':'")
            b = self.ternary()
            return a if c else b
        return c

    PREC = [["||"], ["&&"], ["|"], ["^"], ["&"], ["==", "!="], ["<", ">", "<=", ">="], ["<<", ">>"], ["+", "-"],
            ["*", "/", "%"]]

    def binary(self, level):
        if level == len(self.PREC):
            return self.unary()
        v = self.binary(level + 1)
        while self.peek() in self.PREC[level]:
            op = self.take()
            w = self.binary(level + 1)
            if op in ("/", "%") and w == 0:
                raise CError("division by zero")
            v = {"||": lambda: int(bool(v) or bool(w)), "&&": lambda: int(bool(v) and bool(w)),
                 "|": lambda: v | w, "^": lambda: v ^ w, "&": lambda: v & w, "==": lambda: int(v == w),
                 "!=": lambda: int(v != w), "<": lambda: int(v < w), ">": lambda: int(v > w),
                 "<=": lambda: int(v <= w), ">=": lambda: int(v >= w), "<<": lambda: v << w,
                 ">>": lambda: v >> w, "+": lambda: v + w, "-": lambda: v - w, "*": lambda: v * w,
                 "/": lambda: int(v / w), "%": lambda: v - int(v / w) * w}[op]()
        return v

    def unary(self):
        t = self.peek()
        if t in ("-", "+", "~", "!"):
            self.take()
            v = self.unary()
            return {"-": -v, "+": v, "~": ~v, "!": int(not v)}[t]
        if t == "(":
            # a cast like (uint32_t) or (unsigned int)?
            j, words = self.pos + 1, []
            while j < len(self.toks) and re.fullmatch(r"[A-Za-z_]\w*", self.toks[j]):
                words.append(self.toks[j])
                j += 1
            if words and j < len(self.toks) and self.toks[j] == ")" and all(
                    w in self.INT_CASTS or w in ("long",) for w in words) and not any(
                    w in self.macros or w in self.enums for w in words):
                self.pos = j + 1
                v = self.unary()
                bits, signed = self.INT_CASTS[words[-1] if words[-1] != "int" or len(words) == 1 else words[0]]
                if "unsigned" in words:
                    signed = False
                if words.count("long") == 2:
                    bits = 64
                v &= (1 << bits) - 1
                if signed and v >= 1 << (bits - 1):
                    v -= 1 << bits
                return v
            self.take()
            v = self.ternary()
            if self.take() != ")":
                raise CError("expected ')'")
            return v
        t = self.take()
        if re.fullmatch(r"\d\w*", t):
            return c_number(t)
        if re.fullmatch(r"[A-Za-z_]\w*", t):
            return self.value(t)
        raise CError("unexpected '%s'" % t)


class Macro:
    def __init__(self, name, params, body, file):
        self.name, self.params, self.body, self.file = name, params, body, file
        self.tokens = c_tokens(body)


class HeaderConverter:
    def __init__(self, path, args, cflags):
        self.path, self.args, self.cflags = path, args, cflags
        self.out = Output()
        self.macros = {}       # name -> Macro (last definition wins; #undef removes)
        self.macro_order = []
        self.enums = {}        # enumerator -> value
        self.enum_order = []   # (name, value, file)
        self.structs = {}      # typedef name -> Rec, for types we could translate
        self.struct_order = []
        self.scalar_typedefs = {}
        self.pointer_size = 4

    def preprocess(self):
        cc = self.args.cc or shutil.which("clang") or shutil.which("cc")
        if not cc:
            sys.exit("regs2jai: no C compiler found for preprocessing (use --cc)")
        cmd = [cc, "-E", "-dD", "-ffreestanding", "-x", "c"] + self.cflags + [self.path]
        r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if r.returncode != 0:
            sys.exit("regs2jai: preprocessing failed:\n%s\n%s" % (" ".join(cmd), r.stderr))
        r2 = subprocess.run([cc, "-E", "-dM", "-x", "c"] + self.cflags + ["-"], input="", stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True)
        m = re.search(r"#define __SIZEOF_POINTER__ (\d+)", r2.stdout)
        if m:
            self.pointer_size = int(m.group(1))
        return r.stdout

    def wanted(self, file):
        if self.args.include:
            return any(re.search(p, file) for p in self.args.include) and not any(
                re.search(p, file) for p in self.args.exclude or [])
        if any(re.search(p, file) for p in self.args.exclude or []):
            return False
        if file.startswith("<") or file.startswith("/usr/") or "/lib/clang/" in file or "/include/c++" in file:
            return False
        return True

    def convert(self):
        text = self.preprocess()
        file = self.path
        code = []  # (file, line of code)
        for line in text.split("\n"):
            m = re.match(r'#\s*\d+\s+"((?:\\.|[^"])*)"', line)
            if m:
                file = m.group(1)
                continue
            m = re.match(r"#define\s+([A-Za-z_]\w*)(\(([^)]*)\))?\s*(.*)$", line)
            if m:
                params = None if m.group(2) is None else [p.strip() for p in m.group(3).split(",") if p.strip()]
                if m.group(1) not in self.macros:
                    self.macro_order.append(m.group(1))
                self.macros[m.group(1)] = Macro(m.group(1), params, m.group(4).strip(), file)
                continue
            m = re.match(r"#undef\s+(\w+)", line)
            if m:
                self.macros.pop(m.group(1), None)
                continue
            if line.startswith("#"):
                continue
            code.append((file, line))
        self.parse_code(code)
        return self.emit()

    # -- C declarations

    def parse_code(self, code):
        # Tokens tagged with their file.
        toks, files = [], []
        for file, line in code:
            for t in c_tokens(line):
                toks.append(t)
                files.append(file)
        self.toks, self.files, self.pos = toks, files, 0
        while self.pos < len(toks):
            start = self.pos
            file = files[start]
            try:
                if toks[self.pos] == "typedef":
                    self.pos += 1
                    self.typedef(file)
                elif toks[self.pos] == "enum" and self.find_brace_before_semi():
                    self.pos += 1
                    self.enum_body(file)
                    self.skip_statement()
                else:
                    self.skip_statement()
            except CError as e:
                self.pos = start
                self.skip_statement()
                # Bitfield structs are expected (CMSIS core registers like APSR_Type).
                if self.wanted(file) and (self.args.verbose or "bitfield" not in str(e)):
                    warn("skipped a declaration in %s: %s" % (os.path.basename(file), e))

    def find_brace_before_semi(self):
        for j in range(self.pos, len(self.toks)):
            if self.toks[j] == ";":
                return False
            if self.toks[j] == "{":
                return True
            if self.toks[j] == "(":
                return False
        return False

    def skip_statement(self):
        depth = 0
        while self.pos < len(self.toks):
            t = self.toks[self.pos]
            self.pos += 1
            if t in "({[":
                depth += 1
            elif t in ")}]":
                depth -= 1
                if depth == 0 and t == "}" and (self.pos >= len(self.toks) or self.toks[self.pos] != ";"):
                    # end of a function body (no ';' after it)
                    if self.is_function_body_end():
                        return
            elif t == ";" and depth == 0:
                return

    def is_function_body_end(self):
        # Heuristic: a '}' at depth 0 not followed by an identifier, '[' or ',' ends a function body.
        nxt = self.toks[self.pos] if self.pos < len(self.toks) else None
        return nxt is None or not (re.fullmatch(r"[A-Za-z_]\w*", nxt) or nxt in ",[*")

    def peek(self):
        return self.toks[self.pos] if self.pos < len(self.toks) else None

    def take(self, expect=None):
        t = self.peek()
        if t is None or (expect is not None and t != expect):
            raise CError("expected %s, found %s" % (expect or "more", t))
        self.pos += 1
        return t

    def typedef(self, file):
        while self.peek() in QUALIFIERS or self.peek() in ("__attribute__",):
            self.skip_attr_or_qual()
        if self.peek() in ("struct", "union"):
            is_union = self.take() == "union"
            if re.fullmatch(r"[A-Za-z_]\w*", self.peek() or "") and self.toks[self.pos + 1] == "{":
                self.take()  # tag
            if self.peek() != "{":
                raise CError("typedef of an incomplete struct")
            rec = self.struct_body(is_union)
            names = self.typedef_names()
            for n, is_ptr in names:
                if not is_ptr:
                    if n not in self.structs:
                        self.struct_order.append((n, file))
                    self.structs[n] = rec
            return
        if self.peek() == "enum":
            self.take()
            if re.fullmatch(r"[A-Za-z_]\w*", self.peek() or ""):
                self.take()
            if self.peek() == ":":
                self.take()
                while self.peek() != "{":
                    self.take()
            if self.peek() == "{":
                self.enum_body(file)
            self.skip_statement()
            return
        # typedef <scalar> name;
        words = []
        while re.fullmatch(r"[A-Za-z_]\w*", self.peek() or ""):
            words.append(self.take())
        if self.peek() == ";" and len(words) >= 2:
            base = " ".join(w for w in words[:-1] if w not in QUALIFIERS)
            prim = self.prim_for(base)
            if prim is not None:
                self.scalar_typedefs[words[-1]] = prim
        self.skip_statement()

    def skip_attr_or_qual(self):
        t = self.take()
        if t == "__attribute__":
            self.skip_parens()

    def skip_parens(self):
        self.take("(")
        depth = 1
        while depth:
            t = self.take()
            depth += {"(": 1, ")": -1}.get(t, 0)

    def typedef_names(self):
        names = []
        while True:
            is_ptr = False
            while self.peek() == "*":
                self.take()
                is_ptr = True
            while self.peek() == "__attribute__":
                self.skip_attr_or_qual()
            names.append((self.take(), is_ptr))
            while self.peek() == "__attribute__":
                self.skip_attr_or_qual()
            if self.peek() == ",":
                self.take()
                continue
            break
        self.take(";")
        return names

    def enum_body(self, file):
        self.take("{")
        value = 0
        ev = ConstEval(self.macros, self.enums)
        while self.peek() != "}":
            name = self.take()
            if self.peek() == "=":
                self.take()
                expr = []
                depth = 0
                while not (depth == 0 and self.peek() in (",", "}")):
                    t = self.take()
                    depth += {"(": 1, ")": -1}.get(t, 0)
                    expr.append(t)
                value = ev.eval(expr)
            self.enums[name] = value
            self.enum_order.append((name, value, file))
            value += 1
            if self.peek() == ",":
                self.take()
        self.take("}")

    def prim_for(self, words):
        if words in C_TYPES:
            return PRIMS[C_TYPES[words]]
        if words in ("long", "long int", "signed long", "unsigned long", "unsigned long int"):
            return PRIMS[("u" if "unsigned" in words else "s") + str(self.pointer_size * 8)]
        if words in self.scalar_typedefs:
            return self.scalar_typedefs[words]
        return None

    def struct_body(self, is_union):
        self.take("{")
        fields = []
        while self.peek() != "}":
            while self.peek() in QUALIFIERS or self.peek() == "__attribute__":
                self.skip_attr_or_qual()
            if self.peek() in ("struct", "union"):
                inner_union = self.take() == "union"
                if self.peek() != "{":
                    tag = self.take()
                    base = self.structs.get(tag)
                    if base is None:
                        raise CError("member of unknown type 'struct %s'" % tag)
                    base = Named(tag, base)
                else:
                    base = self.struct_body(inner_union)
                if self.peek() == ";":
                    self.take()
                    fields.append(Field(None, base))
                    continue
            else:
                words = []
                while re.fullmatch(r"[A-Za-z_]\w*", self.peek() or "") and (
                        not words or self.toks[self.pos + 1] not in (";", "[", ",", ":", ")") or
                        self.peek() in ("int", "long", "short", "char")):
                    w = self.take()
                    if w not in QUALIFIERS:
                        words.append(w)
                name_words = " ".join(words)
                base = self.prim_for(name_words)
                if base is None and name_words in self.structs:
                    base = Named(name_words, self.structs[name_words])
                if base is None:
                    raise CError("member of unknown type '%s'" % name_words)
            while True:  # declarators
                t = base
                while self.peek() == "*":
                    self.take()
                    while self.peek() in QUALIFIERS:
                        self.take()
                    t = Ptr(None if t is None else t, self.pointer_size)
                if self.peek() == "(":
                    raise CError("function pointer member")
                name = self.take()
                dims = []
                while self.peek() == "[":
                    self.take()
                    expr = []
                    while self.peek() != "]":
                        expr.append(self.take())
                    self.take("]")
                    dims.append(ConstEval(self.macros, self.enums).eval(expr))
                if self.peek() == ":":
                    raise CError("bitfield member '%s'" % name)
                for d in reversed(dims):
                    t = Arr(t, d)
                fields.append(Field(ident(name), t))
                if self.peek() == ",":
                    self.take()
                    continue
                break
            self.take(";")
        self.take("}")
        return Rec(fields, is_union)

    # -- output

    def emit(self):
        o = self.out
        ev = ConstEval(self.macros, self.enums)
        emitted_types = set()
        for name, file in self.struct_order:
            if not self.wanted(file) or name in emitted_types:
                continue
            emitted_types.add(name)
            o.raw("")
            o.struct(ident(name), self.structs[name])
        enums = [(n, v) for n, v, f in self.enum_order if self.wanted(f)]
        if enums:
            o.raw("")
            o.raw("// ---- Enumerations")
            for n, v in enums:
                o.const(ident(n), int_literal(v, False))
        o.raw("")
        o.raw("// ---- Constants")
        skipped = 0
        for name in self.macro_order:
            m = self.macros.get(name)
            if m is None or not self.wanted(m.file) or name.startswith("__") or not m.body:
                continue
            if m.params is not None:
                self.field_macro(m)
                continue
            ptr = re.fullmatch(r"\(*\s*\(\s*(\w+)\s*\*\s*\)\s*(.*?)\)*", m.body)
            if ptr and ptr.group(1) in self.structs:
                body = m.body.strip()
                inner = body[body.index("*") + 1:].lstrip()[1:]  # after "*)"
                inner = inner.strip()
                while inner.endswith(")") and inner.count("(") < inner.count(")"):
                    inner = inner[:-1].strip()
                try:
                    v = ev.eval(c_tokens(inner))
                except CError:
                    skipped += 1
                    continue
                ref = inner.strip("() ")
                value = ref if re.fullmatch(r"[A-Za-z_]\w*", ref) and ref in self.macros and \
                    self.macros[ref].params is None and self.wanted(self.macros[ref].file) else int_literal(v)
                o.const(ident(name), "cast(*%s) %s" % (ident(ptr.group(1)), value))
                continue
            try:
                v = ev.eval(m.tokens)
            except (CError, RecursionError):
                skipped += 1
                continue
            bit_pattern = "0x" in m.body.lower() or "~" in m.body or "<<" in m.body
            o.const(ident(name), int_literal(v, bit_pattern and v >= 0))
        if self.args.verbose:
            print("regs2jai: %d macros were not constants" % skipped, file=sys.stderr)
        header = ("// Generated by ojay tools/regs2jai.py from %s. Do not edit.\n\n"
                  "#import \"Hardware\";   // volatile_load / volatile_store\n" % os.path.basename(self.path))
        return o.text(header)

    def field_macro(self, m):
        """NAME(x) with NAME_MASK and NAME_SHIFT constants: a field helper procedure."""
        if self.args.no_field_procs or len(m.params) != 1:
            return
        mask, shift = m.name + "_MASK", m.name + "_SHIFT"
        if mask not in self.macros or shift not in self.macros or mask not in m.body or shift not in m.body:
            return
        cast = re.search(r"\(\s*(u?int(?:8|16|32|64)_t)\s*\)", m.body)
        typ = C_TYPES[cast.group(1)] if cast else "u32"
        typ = "u" + typ[1:] if typ.startswith("s") else typ
        try:  # C promotes to int, so e.g. a uint16_t cast with a 32-bit mask still works there
            mask_value = ConstEval(self.macros, self.enums).value(mask)
        except CError:
            return
        while mask_value >> PRIMS[typ].size * 8 and typ != "u64":
            typ = "u%d" % (PRIMS[typ].size * 16)
        self.out.field_proc(ident(m.name), typ, ident(shift), ident(mask))


def main():
    argv = sys.argv[1:]
    cflags = []
    if "--" in argv:
        i = argv.index("--")
        argv, cflags = argv[:i], argv[i + 1:]
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input", help="a .svd file or a C header")
    ap.add_argument("-o", "--output", help="output file (default: stdout)")
    ap.add_argument("--no-enums", action="store_true", help="SVD: omit enumerated field values")
    ap.add_argument("--no-field-procs", action="store_true",
                    help="omit the NAME(x) helper procedures for register fields")
    ap.add_argument("--cc", help="header: C compiler used to preprocess (default: clang, else cc)")
    ap.add_argument("--include", action="append", metavar="REGEX",
                    help="header: keep declarations only from files whose path matches (repeatable)")
    ap.add_argument("--exclude", action="append", metavar="REGEX",
                    help="header: drop declarations from files whose path matches (repeatable)")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args(argv)

    is_svd = args.input.lower().endswith((".svd", ".xml"))
    if not is_svd:
        with open(args.input, "rb") as f:
            is_svd = f.read(512).lstrip().startswith(b"<?xml")
    if cflags and is_svd:
        ap.error("compiler options after '--' only apply to C headers")
    try:
        text = SvdConverter(args.input, args).convert() if is_svd else HeaderConverter(
            args.input, args, cflags).convert()
    except ET.ParseError as e:
        sys.exit("regs2jai: %s: %s" % (args.input, e))
    if args.output:
        with open(args.output, "w") as f:
            f.write(text)
    else:
        sys.stdout.write(text)


if __name__ == "__main__":
    main()
