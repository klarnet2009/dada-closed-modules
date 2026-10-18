#!/usr/bin/env python3
"""chk_off.py btf_mod.txt > chk_off.c : _Static_assert(offsetof(struct S, m) == N) for every named top-level, non-bitfield member of the module-local
structs of the BTF dump (the BTF '/* +off */' comment is the stock offset). Compile it after matt.h: any error = our struct layout differs from the stock."""
import re, sys
txt = open(sys.argv[1], encoding='utf8').read().split('\n')
want = set("""matt_hashinfo matt_shash_table matt_shash_head matt_uhash_table matt_uhash_head matt_sock matt_ulp_ops matt_request_sock
matt_sockaddr matt_src matt_addr matt_port matt_udp_sock matt_dst matt_cc_ctx matt_cc_ops matt_cc_info matt_bbr_info matt_input_ctx matt_sack_block
matt_stats matt_data_stats matt_output_ctx matt_rack matt_mgmt matt_timer_ctx matt_retrans_timer matt_pack_timer matt_nack_timer matt_ping_timer
matt_reo_timer matt_rto_timer matt_stats_timer matthdr matt_skb_cb matt6_sock matt_info matt_creq matt_crsp matt_ping matt_pong
matt_rst matt_pack matt_sack_block_wire matt_nack matt_report msq_tasklet""".split())
print('#include "matt.h"')
print('#include <linux/stddef.h>')
seen = set()
n = 0
i = 0
while i < len(txt):
    m = re.match(r'^struct (\w+) \{$', txt[i])
    if m and m.group(1) in want and m.group(1) not in seen:
        s = m.group(1)
        seen.add(s)
        j = i + 1
        while not txt[j].startswith('}'):
            l = txt[j]
            mm = re.match(r'^\t(?!\t)([^;(]*?\b(\w+)(\[[^\]]*\])*);\t/\* \+(\d+) \*/$', l)
            if mm and ':' not in mm.group(1):
                print('_Static_assert(offsetof(struct %s, %s) == %s, "%s.%s");' % (s, mm.group(2), mm.group(4), s, mm.group(2)))
                n += 1
            j += 1
        i = j
    i += 1
sys.stderr.write('%d asserts, %d structs\n' % (n, len(seen)))
