"""gen_hook2.py: second generator for miscore_input_hook4 (and hook6 with --v6): combos of pre-test / chain / tail"""
import sys, subprocess, os, itertools
v6 = len(sys.argv) > 1 and sys.argv[1] == 'v6'
f = 'miscore/miscore_core.c'
src = open(f, encoding='utf8').read()
name = 'miscore_input_hook6' if v6 else 'miscore_input_hook4'
a0 = src.index('static unsigned int ' + name)
bs = chr(92)
H = 'ip6h' if v6 else 'iph'
P = H + ('->nexthdr' if v6 else '->protocol')
i = src.index('\tif (!%s)\n\t\treturn NF_ACCEPT;\n' % H, a0) + len('\tif (!%s)\n\t\treturn NF_ACCEPT;\n' % H)
j = src.index('\tspin_lock_bh(&miscore_link_lock);', i)
if v6:
    LOGSK = 'MISCORE_DBG("udp6_sk:raddr=%%pI6c,laddr=%%pI6c,proto=%%u,len=%%d,state=%%d%sn",\n\t\t\t\t\t    &ip6h->daddr, &ip6h->saddr, ip6h->nexthdr, skb->len,\n\t\t\t\t\t    sk->sk_state);' % bs
    LOGNO = 'MISCORE_DBG("udp6_no_sk:raddr=%%pI6c,laddr=%%pI6c,proto=%%u,len=%%d%sn",\n\t\t\t\t\t    &ip6h->daddr, &ip6h->saddr, ip6h->nexthdr, skb->len);' % bs
    GET = 'udp6_get_sock'
else:
    LOGSK = 'MISCORE_DBG("udp_sk:raddr=%%pI4,laddr=%%pI4,proto=%%u,id=%%d,len=%%d,state=%%d%sn",\n\t\t\t\t\t    &iph->daddr, &iph->saddr, iph->protocol, ntohs(iph->id),\n\t\t\t\t\t    skb->len, sk->sk_state);' % bs
    LOGNO = 'MISCORE_DBG("udp_no_sk:raddr=%%pI4,laddr=%%pI4,proto=%%u,id=%%d,len=%%d%sn",\n\t\t\t\t\t    &iph->daddr, &iph->saddr, iph->protocol, ntohs(iph->id),\n\t\t\t\t\t    skb->len);' % bs
    GET = 'udp_get_sock'
UDP = '''\t\tif (sk) {
\t\t\t\t%s
\t\t\t} else {
\t\t\t\t%s
\t\t\t\tsk = %s(skb);
\t\t\t\tref = sk != NULL;
\t\t\t\tif (!sk)
\t\t\t\t\treturn NF_ACCEPT;
\t\t\t}
''' % (LOGSK, LOGNO, GET)
PRE = {
 'none': '',
 'tcp_nosk': '\tif (%s == IPPROTO_TCP && !sk)\n\t\treturn NF_ACCEPT;\n' % P,
 'tcp_nosk_unl': '\tif (%s == IPPROTO_TCP && unlikely(!sk))\n\t\treturn NF_ACCEPT;\n' % P,
 'tcp_nosk_full': '\tif (%s == IPPROTO_TCP && (!sk || !sk_fullsock(sk)))\n\t\treturn NF_ACCEPT;\n' % P,
}
CHAIN = {
 'ut_else_notcp': '\tif (%s == IPPROTO_UDP) {\n%s\t} else if (%s != IPPROTO_TCP) {\n\t\treturn NF_ACCEPT;\n\t}\n' % (P, UDP, P),
 'ut_else_notcp_nosk': '\tif (%s == IPPROTO_UDP) {\n%s\t} else if (%s != IPPROTO_TCP || !sk) {\n\t\treturn NF_ACCEPT;\n\t}\n' % (P, UDP, P),
 'ut_else_tcp_else': '\tif (%s == IPPROTO_UDP) {\n%s\t} else if (%s == IPPROTO_TCP) {\n\t} else {\n\t\treturn NF_ACCEPT;\n\t}\n' % (P, UDP, P),
 'sw_u_t': '\tswitch (%s) {\n\tcase IPPROTO_UDP:\n%s\t\tbreak;\n\tcase IPPROTO_TCP:\n\t\tbreak;\n\tdefault:\n\t\treturn NF_ACCEPT;\n\t}\n' % (P, UDP),
 'sw_u_d': '\tswitch (%s) {\n\tcase IPPROTO_UDP:\n%s\t\tbreak;\n\tdefault:\n\t\tif (%s != IPPROTO_TCP)\n\t\t\treturn NF_ACCEPT;\n\t}\n' % (P, UDP, P),
 'notudp_first': '\tif (%s != IPPROTO_UDP) {\n\t\tif (%s != IPPROTO_TCP)\n\t\t\treturn NF_ACCEPT;\n\t} else {\n%s\t}\n' % (P, P, UDP),
}
TAIL = {
 'full': '\tif (%s == IPPROTO_TCP && !sk_fullsock(sk))\n\t\treturn NF_ACCEPT;\n' % P,
}
only = sys.argv[2:] if len(sys.argv) > 2 else None
for (pk, pv), (ck, cv) in itertools.product(PRE.items(), CHAIN.items()):
    if only and (pk + '/' + ck) not in only: continue
    new = src[:i] + pv + cv + TAIL['full'] + src[j:]
    open(f, 'w', encoding='utf8').write(new)
    if os.environ.get('KEEP'): break
    try:
        out = subprocess.run(["bash", "g11_tools/q.sh", 'miscore'], capture_output=True, text=True, env=dict(os.environ, FD=name)).stdout
    finally:
        open(f, 'w', encoding='utf8').write(src)
    err = [l for l in out.splitlines() if 'error' in l][:1]
    summ = [l for l in out.splitlines() if l.startswith('=====')]
    print('%-14s %-20s' % (pk, ck), (err[0][-70:] if err else ' '.join(summ[0].split()[2:])), flush=True)
