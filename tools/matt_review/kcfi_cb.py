#!/usr/bin/env python3
"""kcfi_cb.py vmlinux built.o: kCFI preamble typeid of module callbacks (address-taken functions the kernel calls) vs the typeid
of a kernel function of the same C type in the running vmlinux. Mismatch = the kernel's indirect call traps."""
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from compare_ko_v2 import Elf

PAIRS = """matt_close tcp_close
matt_connect tcp_v4_connect
matt_disconnect tcp_disconnect
matt_accept inet_csk_accept
matt_init_sock tcp_init_sock
matt_destroy_sock tcp_v4_destroy_sock
matt_shutdown tcp_shutdown
matt_setsockopt tcp_setsockopt
matt_getsockopt tcp_getsockopt
matt_recvmsg tcp_recvmsg
matt_do_recv tcp_v4_do_rcv
matt_release_cb tcp_release_cb
matt_hash inet_hash
matt_unhash inet_unhash
matt_inet_poll tcp_poll
matt_inet_listen inet_listen
matt_inet_accept inet_accept
matt_inet_connect inet_stream_connect
matt_inet_shutdown inet_shutdown
matt_inet_sendmsg inet_sendmsg
matt_inet_recvmsg inet_recvmsg
matt_inet_bind inet_bind
matt_inet6_bind inet6_bind
matt_inet_release inet_release
matt_inet6_release inet6_release
matt_inet_getname inet_getname
matt_gso_segment tcp4_gso_segment
matt_gro_receive tcp4_gro_receive
matt_gro_complete tcp4_gro_complete
matt_write_timer_handler tcp_write_timer
matt_rto_timer_handler tcp_write_timer
matt_ping_timer_handler tcp_write_timer
matt_pack_timer_handler tcp_write_timer
matt_nack_timer_handler tcp_write_timer
matt_reo_timer_handler tcp_write_timer
matt_stats_timer_handler tcp_write_timer
matt_tasklet_func tcp_tasklet_func
matt_write_space sk_stream_write_space
matt_inet_sock_destruct inet_sock_destruct
matt_data_wfree tcp_wfree
matt_udp_rcv tcp_v4_do_rcv
matt_udp_err tcp_v4_do_rcv
matt_enter_memory_pressure tcp_enter_memory_pressure
matt_bind raw_bind""".split('\n')


def vm_hash(path, names):
    d = open(path, 'rb').read()
    shoff, = struct.unpack_from('<Q', d, 0x28)
    es, n, si = struct.unpack_from('<HHH', d, 0x3A)
    secs = [struct.unpack_from('<IIQQQQIIQQ', d, shoff + i * es) for i in range(n)]
    symtab = next(s for s in secs if s[1] == 2)
    strtab = secs[symtab[6]]
    out = {}
    for off in range(symtab[4], symtab[4] + symtab[5], 24):
        nm, info, oth, sh, val, size = struct.unpack_from('<IBBHQQ', d, off)
        name = d[strtab[4] + nm:d.index(b'\0', strtab[4] + nm)].decode('latin-1')
        if name in names and (info & 15) == 2 and 0 < sh < n:
            s = secs[sh]
            out.setdefault(name, set()).add(struct.unpack_from('<I', d, s[4] + (val - s[3]) - 4)[0])
    return out


e = Elf(sys.argv[2])
fs = e.functions()
pairs = [l.split() for l in PAIRS if l.strip()]
H = vm_hash(sys.argv[1], set(k for _, k in pairs))
bad = 0
for m, k in pairs:
    mp = fs[m][2] if m in fs else 'MISSING'
    kh = H.get(k)
    ok = kh is not None and mp in kh
    bad += not ok
    print('%-4s %-26s %-12s %-24s %s' % ('OK' if ok else 'BAD?', m, hex(mp) if isinstance(mp, int) else mp, k, ','.join(hex(x) for x in kh) if kh else 'not in vmlinux'))
print('callbacks checked %d, mismatches/unknown %d' % (len(pairs), bad))
