/* symcmp5 selftest: "ours" side with planted polarity/condition bugs (each differs from t_a.c in the condition only) */
extern int g(int);
extern int h2(int, int);

/* pre-G20d bug: only the first range variant */
int in_range(int h, int l, int c) { return (h <= c && c <= l); }
/* inverted condition */
int inv_branch(int a, int b) { if (a >= b) return g(1); return g(2); }
/* signed -> unsigned */
int sign_cmp(int a, int b) { if ((unsigned)a < (unsigned)b) return g(1); return g(2); }
/* < -> <= */
int ret_bool(int a) { return a <= 5; }
/* && -> || */
int ccmp_and(int a, int b) { return a < 5 || b > 3; }
/* same logic, other source form, expect-hint changes block order */
int diamond(int a, int b) { int r = 0; if (__builtin_expect(!(a >= b), 0)) { r = g(1); } else { r = g(2); } return g(3) + r; }
/* boundary 20 -> 21 */
int range_call(int c) { if (c >= 10 && c < 21) return g(1); return g(0); }
/* && -> || on the bool materialisation */
int bool_chain(int a, int b, int c) { int x = a < b; int y = c != 0; if (x || y) return g(7); return g(8); }
/* args swapped (same atom, other orientation: a>b vs a<b) */
int csel_pick(int a, int b) { return a < b ? g(1) : g(2); }
