/* symcmp5 selftest: "stock" side. Compile: clang --target=aarch64-linux-gnu -O2 -ffreestanding -fno-builtin -c */
extern int g(int);
extern int h2(int, int);

int in_range(int h, int l, int c) { return (h <= c && c <= l) || (l <= c && c <= h); }
int inv_branch(int a, int b) { if (a < b) return g(1); return g(2); }
int sign_cmp(int a, int b) { if (a < b) return g(1); return g(2); }
int ret_bool(int a) { return a < 5; }
int ccmp_and(int a, int b) { return a < 5 && b > 3; }
int diamond(int a, int b) { int r = 0; if (a < b) { r = g(1); } else { r = g(2); } return r + g(3); }
int range_call(int c) { if (c >= 10 && c < 20) return g(1); return g(0); }
int bool_chain(int a, int b, int c) { int x = a < b; int y = c != 0; if (x && y) return g(7); return g(8); }
int csel_pick(int a, int b) { return a > b ? g(1) : g(2); }
