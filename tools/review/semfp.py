"""semfp.py <stock.ko> <built.ko|.o> [-a] [-q] [func...]
Semantic fingerprint diff of functions whose bytes differ (or the given ones; -a = all functions).
Per function compares, stock vs built:
  CALLS  ordered list of call targets (bl/b to external or local functions; relocs resolved like compare_ko)
  REFS   multiset of non-call relocation targets (strings by content, objects by name)
  IMM    multiset of immediates of data-processing instructions (mov/movk/cmp/add/sub/and/orr/...), sp/x29 frame ops excluded
  MEM    multiset of (access mnemonic, offset) of loads/stores whose base is not sp/x29 (struct field accesses)
  COND   multiset of condition codes (b.cc / cset / csel / ccmp / cinc ...), informational: layout flips eq/ne legitimately
Output: per function the stock-only / built-only items of each class. Empty classes => no fingerprint difference.
"""
import re, subprocess, sys, collections
sys.path.insert(0, "<build-dir>")
from compare_ko import Elf

OBJDUMP = "<build-dir>"
args = sys.argv[1:]
ALL = "-a" in args; QUIET = "-q" in args; SEQ = "-s" in args; ARGS = "-g" in args
args = [a for a in args if a not in ("-a", "-q", "-s", "-g")]
stock, built, want = args[0], args[1], set(args[2:])

FRAME = re.compile(r"\b(sp|x29|w29)\b")
DATA_OPS = ("mov", "movk", "movz", "movn", "cmp", "cmn", "add", "adds", "sub", "subs", "and", "ands", "orr", "eor", "tst",
            "ccmp", "ccmn", "lsl", "lsr", "asr", "ubfx", "sbfx", "ubfiz", "sbfiz", "bfi", "bfxil", "mul", "madd", "msub", "tbz", "tbnz")
MEMOPS = re.compile(r"^(ldr|ldrb|ldrh|ldrsw|ldrsb|ldrsh|ldur|ldurb|ldurh|ldursw|str|strb|strh|stur|sturb|sturh|ldp|stp|ldar|ldarb|ldapr|ldaprb|stlr|stlrb|ldxr|stxr|ldaxr|stlxr|ldadd\w*|swp\w*|cas\w*|ldset\w*|ldclr\w*|stset\w*|stadd\w*|prfm)$")
CONDS = ("eq", "ne", "hs", "lo", "cs", "cc", "mi", "pl", "vs", "vc", "hi", "ls", "ge", "lt", "gt", "le")


def listing(path):
    e = Elf(path)
    secidx = {s["name"]: i for i, s in enumerate(e.secs)}
    secsym = {}
    for si, s in enumerate(e.syms):
        if s["type"] == 3 and s["shndx"] not in secsym:
            secsym[s["shndx"]] = si
    fsyms = {}
    for s in e.syms:
        if s["type"] == 2 and s["size"] and s["shndx"] < len(e.secs):
            fsyms.setdefault(e.secs[s["shndx"]]["name"], []).append((s["val"], s["size"], s["name"]))
    out = subprocess.run([OBJDUMP, "-dr", "--no-show-raw-insn", path], capture_output=True, text=True).stdout
    funcs, cur, cursec = {}, None, None
    for l in out.splitlines():
        m = re.match(r"^Disassembly of section (\S+):", l)
        if m:
            cursec = m.group(1); continue
        m = re.match(r"^([0-9a-f]{16}) <(.*)>:$", l)
        if m:
            cur = m.group(2); funcs.setdefault(cur, []); continue
        if cur is None:
            continue
        m = re.match(r"^\s+[0-9a-f]+:\s+(R_AARCH64_\w+)\s+(\S+)$", l)
        if m:
            t, tgt = m.groups()
            mm = re.match(r"^(.*?)(?:\+0x([0-9a-f]+))?$", tgt)
            name, add = mm.group(1), int(mm.group(2) or "0", 16)
            if name.startswith((".text", ".init.text", ".exit.text")) and name in secidx:
                res = "@" + re.sub(r"\+0$", "", e.owner(secidx[name], add))
            elif name in secidx and secidx[name] in secsym:
                try:
                    res = e.target(secsym[secidx[name]], add)
                except Exception:
                    res = tgt
            else:
                res = tgt
            res = re.sub(r"\+0$", "", res)
            if funcs[cur]:
                funcs[cur][-1][2].append((t, res))
            continue
        m = re.match(r"^\s+([0-9a-f]+):\s+(\S+)\s*(.*)$", l)
        if m:
            off, mn, ops = m.groups()
            ops = re.sub(r"\s+//.*$", "", ops)
            tgt = None
            mm = re.search(r"0x[0-9a-f]+ <([^>]*)>", ops)
            if mm:
                tgt = mm.group(1)
            funcs[cur].append([mn, ops, [], tgt, int(off, 16)])
    for k, v in funcs.items():
        while v and v[-1][0] == ".word":
            v.pop()
    return funcs


def fp(insns, fname):
    calls, refs, imm, mem, cond = [], collections.Counter(), collections.Counter(), collections.Counter(), collections.Counter()
    for mn, ops, rel, tgt, *_ in insns:
        for t, r in rel:
            if t in ("R_AARCH64_CALL26", "R_AARCH64_JUMP26"):
                calls.append(r)
            elif t.endswith("LO12_NC") and t.startswith("R_AARCH64_ADD"):
                continue
            else:
                refs[r] += 1
        if mn in ("bl",) and not rel and tgt:
            base = tgt.split("+")[0]
            if base != fname:
                calls.append("@" + base)
        if mn.startswith("b.") or mn in ("cset", "csetm", "csel", "csinc", "csinv", "csneg", "cinc", "cinv", "cneg", "ccmp", "ccmn", "fcsel"):
            c = mn[2:] if mn.startswith("b.") else ops.split(",")[-1].strip()
            if c in CONDS:
                cond[c] += 1
        if MEMOPS.match(mn):
            if "[sp" in ops or "[x29" in ops:
                continue
            mm = re.search(r"\[(x\d+|w\d+)(?:, #(-?0x[0-9a-f]+|-?\d+))?\]", ops)
            if mm:
                o = mm.group(2) or "0"
                o = int(o, 16) if "0x" in o else int(o)
                # register width matters (w vs x)
                w = ops.split(",")[0].strip()[0]
                mem["%s.%s+%#x" % (mn, w, o)] += 1
            continue
        base = mn.split(".")[0]
        if base in DATA_OPS:
            if FRAME.search(ops):
                continue
            if mn in ("tbz", "tbnz"):
                mm = re.search(r"#(\d+)", ops)
                if mm:
                    imm["%s bit%s" % (mn[:3], mm.group(1))] += 1
                continue
            for x in re.findall(r"#(-?0x[0-9a-f]+|-?\d+)", ops):
                v = int(x, 16) if "0x" in x else int(x)
                sh = re.search(r"lsl #(\d+)", ops)
                imm["%s %#x%s" % ("cmp" if base in ("cmp", "cmn", "ccmp", "ccmn", "tst") else base, v, "" if not sh else "")] += 1
    return calls, refs, imm, mem, cond


def callargs(insns, fname):
    """multiset of 'callee(argN=value,...)' with argument values known from straight-line code before the call"""
    targets = set()
    for mn, ops, rel, tgt, off in insns:
        if mn in ("b", "cbz", "cbnz", "tbz", "tbnz") or mn.startswith("b."):
            m = re.search(r"0x([0-9a-f]+) <", ops)
            if m:
                targets.add(int(m.group(1), 16))
    regs = {}
    out = collections.Counter()
    prev_uncond = False

    def rn(x):
        x = x.strip()
        if x in ("wzr", "xzr"):
            return "zr"
        m = re.match(r"^[wx](\d+)$", x)
        return int(m.group(1)) if m else None

    for mn, ops, rel, tgt, off in insns:
        if off in targets or prev_uncond:
            regs = {}
        prev_uncond = mn in ("b", "br", "ret")
        o = [x.strip() for x in re.split(r",(?![^\[]*\])", ops)] if ops else []
        callee = None
        for tt, r in rel:
            if tt in ("R_AARCH64_CALL26", "R_AARCH64_JUMP26"):
                callee = r
        if mn == "bl" and callee is None and tgt:
            callee = "@" + tgt.split("+")[0]
        if mn in ("bl", "b") and callee:
            args = ",".join("a%d=%s" % (i, regs[i]) for i in range(8) if i in regs)
            out["%s(%s)%s" % (callee, args, " [tail]" if mn == "b" else "")] += 1
            for i in range(19):
                regs.pop(i, None)
            continue
        if mn == "blr":
            for i in range(19):
                regs.pop(i, None)
            continue
        if not o:
            continue
        if mn in ("cmp", "cmn", "tst", "ccmp", "ccmn", "cbz", "cbnz", "tbz", "tbnz", "fcmp") or mn.startswith("b"):
            continue
        if MEMOPS.match(mn) and mn.startswith("st"):
            continue
        if mn in ("ldp", "ldnp"):
            for x in o[:2]:
                r = rn(x)
                if isinstance(r, int):
                    regs.pop(r, None)
            continue
        d = rn(o[0])
        if not isinstance(d, int):
            continue
        val = None
        if mn in ("mov", "movz") and len(o) == 2:
            if o[1].startswith("#"):
                val = int(o[1][1:], 0)
            elif o[1] == "sp":
                val = "stack"
            else:
                s = rn(o[1])
                val = 0 if s == "zr" else (regs.get(s) if isinstance(s, int) else None)
        elif mn == "movn" and len(o) >= 2:
            val = ~int(o[1][1:], 0)
        elif mn == "movk" and isinstance(regs.get(d), int):
            m = re.search(r"#(0x[0-9a-f]+|\d+)(?:, lsl #(\d+))?", ops)
            if m:
                sh = int(m.group(2) or 0)
                v = int(m.group(1), 0)
                val = (regs[d] & ~(0xffff << sh)) | (v << sh)
        elif mn == "orr" and len(o) == 3 and rn(o[1]) == "zr" and o[2].startswith("#"):
            val = int(o[2][1:], 0)
        elif mn == "adrp":
            val = "&" + (rel[0][1] if rel else "?")
        elif mn == "adr":
            val = "&" + (rel[0][1] if rel else "adr")
        elif mn == "add" and len(o) == 3 and o[2].startswith("#"):
            s = rn(o[1])
            if o[1] in ("sp", "x29"):
                val = "stack"
            elif isinstance(s, int) and isinstance(regs.get(s), str) and regs[s].startswith("&") and int(o[2][1:], 0) == 0:
                val = regs[s]
            else:
                val = "fld+" + o[2][1:]
        elif mn == "sub" and len(o) == 3 and o[1] in ("sp", "x29"):
            val = "stack"
        elif MEMOPS.match(mn) and mn.startswith("ld"):
            m = re.search(r"\[(\w+)(?:, #(-?0x[0-9a-f]+|-?\d+))?\]", ops)
            if m and m.group(1) not in ("sp", "x29"):
                rr = [x[1] for x in rel]
                val = "ld%s[%s]" % (mn[2:] + o[0][0], rr[0] if rr else (m.group(2) or "0"))
            elif m:
                val = "ldstack"
        elif mn in ("cset", "csetm"):
            val = "cset." + o[-1]
        if val is None:
            regs.pop(d, None)
        else:
            regs[d] = val
    return out


def cdiff(a, b):
    sa, sb = a - b, b - a
    return sorted(sa.elements()), sorted(sb.elements())


A, B = listing(stock), listing(built)
ea, eb = Elf(stock).functions(), Elf(built).functions()
names = [n for n in A if n in B and not n.startswith(".") and n in ea and n in eb]
for n in names:
    if want and n not in want:
        continue
    if not want and not ALL and ea[n][0] == eb[n][0]:
        continue
    ca, ra, ia, ma, oa = fp(A[n], n)
    cb, rb, ib, mb, ob = fp(B[n], n)
    out = []
    if ca != cb:
        if collections.Counter(ca) == collections.Counter(cb):
            out.append("  CALLS: same multiset, ORDER differs")
            import difflib
            for l in list(difflib.unified_diff(ca, cb, lineterm="", n=1))[2:]:
                out.append("     " + l)
        else:
            x, y = cdiff(collections.Counter(ca), collections.Counter(cb))
            out.append("  CALLS stock-only: %s\n  CALLS built-only: %s" % (x, y))
    x, y = cdiff(ra, rb)
    if x or y: out.append("  REFS stock-only: %s\n  REFS built-only: %s" % (x, y))
    x, y = cdiff(ia, ib)
    if x or y: out.append("  IMM stock-only: %s\n  IMM built-only: %s" % (x, y))
    x, y = cdiff(ma, mb)
    if x or y: out.append("  MEM stock-only: %s\n  MEM built-only: %s" % (x, y))
    if ARGS:
        x, y = cdiff(callargs(A[n], n), callargs(B[n], n))
        if x or y:
            out.append("  ARGS stock-only:\n      " + "\n      ".join(x) + "\n  ARGS built-only:\n      " + "\n      ".join(y))
    x, y = cdiff(oa, ob)
    if (x or y) and not QUIET: out.append("  COND stock-only: %s  built-only: %s" % (x, y))
    same = ea[n][0] == eb[n][0]
    print("=== %s: %s, insns %d/%d, calls %d/%d %s" % (n, "BYTES-IDENTICAL" if same else "bytes differ", len(A[n]), len(B[n]), len(ca), len(cb), "" if out else "-> fingerprint IDENTICAL"))
    for o in out:
        print(o)
    if SEQ:
        import difflib
        def seq(ins):
            r = []
            for mn, ops, rel, tgt, *_ in ins:
                for t, x in rel:
                    if t.endswith("LO12_NC") and t.startswith("R_AARCH64_ADD"):
                        continue
                    r.append(("call " if t in ("R_AARCH64_CALL26", "R_AARCH64_JUMP26") else "ref  ") + x[:90])
                if mn == "bl" and not rel and tgt:
                    r.append("call @" + tgt.split("+")[0])
            return r
        for l in list(difflib.unified_diff(seq(A[n]), seq(B[n]), lineterm="", n=2))[2:]:
            print("     " + l)
