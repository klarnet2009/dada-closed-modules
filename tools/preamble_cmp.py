#!/usr/bin/env python3
"""kCFI type-hash audit that compare_ko.py does NOT do: compare_ko compares the bytes inside each function symbol, but the kCFI hash word sits in
the 4 bytes BEFORE the symbol. This compares that word per function between the OS4 stock .ko and the rebuilt one.
Usage: preamble_cmp.py <stock.ko> <built.ko> [more pairs...]      prints one line per pair: name, functions compared, mismatches (+ examples)."""
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from verify_pkg import Ko


from verify_pkg import preambles  # noqa: E402,F811  (global + address-taken functions only)


def compare(stock, built):
    a, b = preambles(Ko(stock)), preambles(Ko(built))
    common = [n for n in a if n in b]
    bad = [n for n in common if a[n] != b[n]]
    return len(common), bad, a, b


if __name__ == "__main__":
    args = sys.argv[1:]
    for i in range(0, len(args), 2):
        n, bad, a, b = compare(args[i], args[i + 1])
        print(f"{os.path.basename(args[i + 1])}: {n} functions, preamble mismatches {len(bad)}" +
              ("" if not bad else " : " + ", ".join(f"{x}(stock {a[x]:#x} built {b[x]:#x})" for x in bad[:4])))
