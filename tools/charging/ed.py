"""ed.py helper: from ed import rep, rep_fn, sub
rep(path, old, new[, count=1])  exact text replacement (CRLF-safe, asserts the match count); text may use \t escapes, write backslashes as BS (see below)
rep_fn(path, 'func_name', new_body_text)  replace the whole definition of a top-level function (from its signature line to the closing '}' at column 0)
Texts are python strings: write a C backslash as chr(92) / the token '@BS@' (replaced by a backslash)."""
import re


def _rd(p):
    return open(p, 'rb').read().decode('utf-8').replace('\r\n', '\n')


def _wr(p, s):
    open(p, 'wb').write(s.replace('\n', '\r\n').encode('utf-8'))


def rep(p, old, new, count=1):
    old = old.replace('@BS@', chr(92)); new = new.replace('@BS@', chr(92))
    s = _rd(p)
    n = s.count(old)
    assert n == count, f"{p}: expected {count} match(es), found {n} for {old[:60]!r}"
    _wr(p, s.replace(old, new))


def rep_fn(p, name, new):
    new = new.replace('@BS@', chr(92))
    s = _rd(p)
    m = re.search(r'^(?:static\s+)?[A-Za-z_][\w \*]*?\b' + re.escape(name) + r'\([^;{}]*\)\s*\n?\{.*?^\}\n', s, re.S | re.M)
    assert m, f"function {name} not found in {p}"
    _wr(p, s[:m.start()] + new + s[m.end():])


def sub(p, pat, new, count=1, flags=re.S | re.M):
    s = _rd(p)
    s2, n = re.subn(pat, new.replace('@BS@', chr(92)), s, flags=flags)
    assert n == count, f"{p}: {n} matches for {pat!r}"
    _wr(p, s2)
