#!/usr/bin/env python3
# server-side: genksyms CRC of mca_smartchg_if_ops_register for header spelling variants (target OS4 0x85e2f9d0, OS3 0xf068973d)
import subprocess, re, os, sys
tmp = "<work-dir>"; os.makedirs(tmp + "/mca/smartchg", exist_ok=True)
def crc(body):
    h = "#ifndef X\n#define X\n#include <linux/types.h>\n" + body + "\nint mca_smartchg_if_ops_register(struct mca_smartchg_if_ops *ops);\n#endif\n"
    open(tmp + "/mca/smartchg/smart_chg_class.h", "w").write(h)
    open(tmp + "/t.c", "w").write("#include <linux/export.h>\n#include <mca/smartchg/smart_chg_class.h>\nEXPORT_SYMBOL(mca_smartchg_if_ops_register);\n")
    out = subprocess.run(["<work-dir>", tmp + "/t.c"], capture_output=True, text=True, env=dict(os.environ, INC="-I" + tmp)).stdout
    m = re.search(r"mca_smartchg_if_ops_register\s+0x([0-9a-f]+)", out)
    return m.group(1) if m else out[:200]
cbs = ["set_delta_fv", "set_delta_ichg", "set_fcc", "set_soc_limit_sts", "set_pwr_boost_sts", "set_wls_super_sts", "set_wls_quiet_sts"]
def struct(named, upd, last):
    p = "void *data, int value" if named else "void *, int"
    s = "struct mca_smartchg_if_ops {\n int type;\n void *data;\n"
    for c in cbs: s += " int (*%s)(%s);\n" % (c, p)
    s += " int (*update_baa_para)(%s);\n" % upd
    if last: s += " %s\n" % last
    return s + "};"
variants_upd = {"named_char": "void *data, char *name, int value1, int value2", "unnamed_char": "void *, char *, int, int", "unnamed_cchar": "void *, const char *, int, int", "unnamed_void": "void *, void *, int, int", "unnamed_u8p": "void *, u8 *, int, int", "named_void": "void *data, void *para, int size1, int size2"}
for named in (True, False):
    for k, u in variants_upd.items():
        print("OS3", named, k, crc(struct(named, u, "")))
print("---- OS4 target 85e2f9d0")
u = "void *data, char *name, int value1, int value2"
for last in ["bool (*is_support_pps_100w)(void);", "int (*is_support_pps_100w)(void);", "bool (*is_support_pps_100w)();", "_Bool (*is_support_pps_100w)(void);", "u8 (*is_support_pps_100w)(void);", "unsigned char (*is_support_pps_100w)(void);", "bool (*is_support_pps_100w)(void *);", "bool (*is_support_pps_100w)(void *data);", "int (*is_support_pps_100w)(void *);", "void (*is_support_pps_100w)(void);", "bool (*is_support_pps_100w)(int);", "bool *is_support_pps_100w;", "bool is_support_pps_100w;", "int is_support_pps_100w;"]:
    print(last, crc(struct(True, u, last)))
print("---- brute")
import itertools
ups = ["char *", "const char *", "void *", "u8 *", "unsigned char *", "char *const", "int *", "u32 *", "const void *"]
lasts = ["bool (*is_support_pps_100w)(void);", "int (*is_support_pps_100w)(void);", "bool (*is_support_pps_100w)();", "int (*is_support_pps_100w)();", "void (*is_support_pps_100w)(void);", "bool (*is_support_pps_100w)(void *);"]
for up in ups:
    for last in lasts:
        r = crc(struct(True, "void *data, %s name, int value1, int value2" % up, last))
        if r == "85e2f9d0": print("MATCH", up, last)
print("done")
