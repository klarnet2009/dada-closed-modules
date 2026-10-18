"""gen_hook.py : brute-force source shapes for miscore_input_hook4. Prints diff-line count per variant."""
import sys, subprocess, os, itertools
f = 'miscore/miscore_core.c'
src = open(f, encoding='utf8').read()
a0 = src.index('static unsigned int miscore_input_hook4')
i = src.index('\tif (iph->protocol == IPPROTO_UDP) {', a0)
j = src.index('\tspin_lock_bh(&miscore_link_lock);', i)

LOGSK = '''MISCORE_DBG("udp_sk:raddr=%pI4,laddr=%pI4,proto=%u,id=%d,len=%d,state=%d@@N",
				    &iph->daddr, &iph->saddr, iph->protocol, ntohs(iph->id),
				    skb->len, sk->sk_state);'''
LOGNO = '''MISCORE_DBG("udp_no_sk:raddr=%pI4,laddr=%pI4,proto=%u,id=%d,len=%d@@N",
				    &iph->daddr, &iph->saddr, iph->protocol, ntohs(iph->id),
				    skb->len);'''
TAIL = '''	if (iph->protocol == IPPROTO_TCP && !sk_fullsock(sk))
		return NF_ACCEPT;
'''
def udp(order, refset):
    global LOGSK, LOGNO
    LOGSK = LOGSK.replace('@@N', chr(92)+'n'); LOGNO = LOGNO.replace('@@N', chr(92)+'n')
    sk = '\t\t\tif (sk) {\n\t\t\t\t%s\n%s\t\t\t}' % (LOGSK, '\t\t\t\tref = false;\n' if refset else '')
    no = '{\n\t\t\t\t%s\n\t\t\t\tsk = udp_get_sock(skb);\n\t\t\t\tref = sk != NULL;\n\t\t\t\tif (!sk)\n\t\t\t\t\treturn NF_ACCEPT;\n\t\t\t}' % LOGNO
    if order == 0:
        return sk + ' else ' + no + '\n'
    sk2 = '\t\t\tif (!sk) ' + no + ' else {\n\t\t\t\t%s\n%s\t\t\t}\n' % (LOGSK, '\t\t\t\tref = false;\n' if refset else '')
    return sk2
def build(d, uo, rs):
    U = udp(uo, rs)
    T = '\t\tif (!sk)\n\t\t\treturn NF_ACCEPT;\n'
    if d == 'sw_tu':
        b = '\tswitch (iph->protocol) {\n\tcase IPPROTO_TCP:\n' + T + '\t\tbreak;\n\tcase IPPROTO_UDP:\n' + U + '\t\tbreak;\n\tdefault:\n\t\treturn NF_ACCEPT;\n\t}\n'
    elif d == 'sw_ut':
        b = '\tswitch (iph->protocol) {\n\tcase IPPROTO_UDP:\n' + U + '\t\tbreak;\n\tcase IPPROTO_TCP:\n' + T + '\t\tbreak;\n\tdefault:\n\t\treturn NF_ACCEPT;\n\t}\n'
    elif d == 'sw_dtu':
        b = '\tswitch (iph->protocol) {\n\tdefault:\n\t\treturn NF_ACCEPT;\n\tcase IPPROTO_TCP:\n' + T + '\t\tbreak;\n\tcase IPPROTO_UDP:\n' + U + '\t\tbreak;\n\t}\n'
    elif d == 'if_tu':
        b = '\tif (iph->protocol == IPPROTO_TCP) {\n' + T + '\t} else if (iph->protocol == IPPROTO_UDP) {\n' + U + '\t} else {\n\t\treturn NF_ACCEPT;\n\t}\n'
    elif d == 'if_ut':
        b = '\tif (iph->protocol == IPPROTO_UDP) {\n' + U + '\t} else if (iph->protocol == IPPROTO_TCP) {\n' + T + '\t} else {\n\t\treturn NF_ACCEPT;\n\t}\n'
    elif d == 'if_pre':
        b = '\tif (iph->protocol != IPPROTO_TCP && iph->protocol != IPPROTO_UDP)\n\t\treturn NF_ACCEPT;\n\tif (iph->protocol == IPPROTO_TCP) {\n' + T + '\t} else {\n' + U + '\t}\n'
    elif d == 'if_pre2':
        b = '\tif (iph->protocol != IPPROTO_UDP && iph->protocol != IPPROTO_TCP)\n\t\treturn NF_ACCEPT;\n\tif (iph->protocol == IPPROTO_UDP) {\n' + U + '\t} else {\n' + T + '\t}\n'
    elif d == 'if_tcpfirst2':
        b = '\tif (iph->protocol == IPPROTO_TCP && !sk)\n\t\treturn NF_ACCEPT;\n\tif (iph->protocol == IPPROTO_UDP) {\n' + U + '\t} else if (iph->protocol != IPPROTO_TCP) {\n\t\treturn NF_ACCEPT;\n\t}\n'
    return b + TAIL
res = []
for d in ['sw_tu', 'sw_ut', 'sw_dtu', 'if_tu', 'if_ut', 'if_pre', 'if_pre2', 'if_tcpfirst2']:
    for uo in (0, 1):
        for rs in (0, 1):
            new = src[:i] + build(d, uo, rs) + src[j:]
            open(f, 'w', encoding='utf8').write(new)
            try:
                out = subprocess.run(["bash", "g11_tools/q.sh", 'miscore'], capture_output=True, text=True, env=dict(os.environ, FD='miscore_input_hook4')).stdout
            finally:
                open(f, 'w', encoding='utf8').write(src)
            err = [l for l in out.splitlines() if 'error' in l][:1]
            summ = [l for l in out.splitlines() if l.startswith('=====')]
            print(d, uo, rs, (err[0][-60:] if err else summ[0].split()[-1]), flush=True)
