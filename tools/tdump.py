"""tdump.py <stock.ko> [event]: dump the trace-event metadata of a stock module (fields table with type/name strings, print_fmt,
event class pointers). Runs on the server (needs compare_ko.Elf in <work-dir>)."""
import struct, sys
sys.path.insert(0, '<work-dir>')
from compare_ko import Elf, cstr


def sym_data(e, name):
    for i, s in enumerate(e.syms):
        if s['name'] == name and s['type'] == 1 and s['shndx']:
            sec = e.data(s['shndx'])
            return s, sec[s['val']:s['val'] + s['size']]
    return None, None


def reloc_map(e, s):
    """offset-in-object -> (symname-or-section, addend, string if rodata)"""
    out = {}
    for off, t, si, add in e.relocs.get(s['shndx'], []):
        if s['val'] <= off < s['val'] + s['size']:
            sy = e.syms[si]
            if sy['type'] == 3:
                sn = e.secs[sy['shndx']]['name']
                if sn.startswith('.rodata'):
                    out[off - s['val']] = ('str', cstr(e.data(sy['shndx']), add))
                else:
                    out[off - s['val']] = ('sec', f'{sn}+{add}')
            else:
                out[off - s['val']] = ('sym', sy['name'] + (f'+{add}' if add else ''))
    return out


def events(e):
    ev = []
    for s in e.syms:
        if s['type'] == 2 and s['name'].startswith('__traceiter_'):
            ev.append(s['name'][len('__traceiter_'):])
    return ev


def fields(e, ev):
    s, d = sym_data(e, 'trace_event_fields_' + ev)
    r = reloc_map(e, s)
    out = []
    for i in range(0, len(d), 40):
        ty = r.get(i); nm = r.get(i + 8)
        if ty is None:
            break
        size, align, signed, ftype, ln = struct.unpack_from('<iiiii', d, i + 16)
        out.append(dict(type=ty[1], name=nm[1], size=size, align=align, signed=signed, ftype=ftype, len=ln))
    return out


def print_fmt(e, ev):
    s, d = sym_data(e, 'print_fmt_' + ev)
    return d.rstrip(b'\0').decode()


if __name__ == '__main__':
    e = Elf(sys.argv[1])
    for ev in ([sys.argv[2]] if len(sys.argv) > 2 else events(e)):
        print('==', ev)
        for f in fields(e, ev):
            print('  ', f)
        print('  fmt:', print_fmt(e, ev))
