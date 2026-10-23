/* symcmp5 selftest: equivalent rewrites of t_a.c (different source form / operand order / block order). Must NOT be reported. */
extern int g(int);
extern int h2(int, int);

int in_range(int h, int l, int c) { return (c >= h && c <= l) || (c >= l && c <= h); }
int inv_branch(int a, int b) { if (b > a) return g(1); return g(2); }
int sign_cmp(int a, int b) { if (!(a >= b)) return g(1); return g(2); }
int ret_bool(int a) { return !(a >= 5); }
int ccmp_and(int a, int b) { if (a >= 5) return 0; return 3 < b; }
int diamond(int a, int b) { int r; if (__builtin_expect(a >= b, 1)) { r = g(2); } else { r = g(1); } return r + g(3); }
int range_call(int c) { if (c < 10) return g(0); if (c >= 20) return g(0); return g(1); }
int bool_chain(int a, int b, int c) { if (c == 0) return g(8); if (b <= a) return g(8); return g(7); }
int csel_pick(int a, int b) { return b < a ? g(1) : g(2); }
