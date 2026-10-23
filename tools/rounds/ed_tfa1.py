import re, sys
D = '<workdir>/work/re/src/tfa98xx_dlkm/'


def edit(fn, pairs):
    s = open(D + fn, encoding='latin1', newline='').read()
    crlf = '\r\n' in s
    for old, new in pairs:
        if crlf:
            old = old.replace('\n', '\r\n'); new = new.replace('\n', '\r\n')
        if s.count(old) != 1:
            print('NOT UNIQUE/FOUND', fn, s.count(old), repr(old[:60]))
            continue
        s = s.replace(old, new)
    open(D + fn, 'w', encoding='latin1', newline='').write(s)


edit('tfa98xx.c', [
    ('\t\tpr_info("[0x%x] dsp_msg_read error: %d\\n",\n\t\t\ttfa98xx->i2c->addr, error);\n', ''),
    ('\t\tif (err != 0)\n\t\t\tpr_err("[0x%x] dsp_msg error: %d\\n", i2c->addr, err);\n\n', ''),
    ('\t\t\t\tpr_debug("[0x%x] dsp_msg error: %d\\n",\n\t\t\t\t\ttfa98xx->i2c->addr, error);\n', ''),
    ('\t\t\tpr_err("Invalid Codec master mode\\n");\n', ''),
    ('no need to probe\\n");', 'no need to probe");'),
])
edit('tfa_service.h', [('  #define TFA98XX_API_REV_STR "v6.5.3"/*TFA98XX_GIT_VERSIONS*/', '  #define TFA98XX_API_REV_STR "v6.13._xiaomi"/*TFA98XX_GIT_VERSIONS*/'),
                       ('#else\n  #define TFA98XX_API_REV_STR "v6.5.3"\n', '#else\n  #define TFA98XX_API_REV_STR "v6.13._xiaomi"\n')])
edit('tfa_dsp.c', [
    ('\tif (rc < 3)\n\t\tpr_err("invalid version string.\\n");\n', '\t(void)rc;\n'),
    ('\t\t\tpr_err("%s: kmem alloc fail!\\n", __func__);\n', ''),
    ('\t\t\tpr_err("%s: kmem alloc for bytes fail\\n", __func__);\n', ''),
    ('\t\tpr_err("Calibration failed!\\n");\n\t\terr = Tfa98xx_Error_Bad_Parameter;', '\t\tpr_err("Calibration failed! \\n");\n\t\terr = Tfa98xx_Error_Bad_Parameter;'),
    ('\t\t\tpr_err("Calibration failed!\\n");\n\t\t\terror = Tfa98xx_Error_Bad_Parameter;', '\t\t\tpr_err("Calibration failed! \\n");\n\t\t\terror = Tfa98xx_Error_Bad_Parameter;'),
    ('\t\t\tpr_debug(\n\t\t\t\t"Error: Current device has no secondary Re25 channel\\n");', '\t\t\tpr_info("Error: Current device has no secondary Re25 channel \\n");'),
])
edit('tfa_container.c', [
    ('\t\t\t\tpr_debug("Writing cmd=0x%02x%02x%02x\\n",\n', '\t\t\t\tpr_debug("Writing cmd=0x%02x%02x%02x \\n",\n'),
])
edit('tfa_init.c', [('\t\t\tpr_debug("check_correct\\n");\n', '')])
s = open(D + 'tfa_dsp.c', encoding='latin1', newline='').read()
n = s.count('pr_err("\\nError: Unable to read revid')
s = s.replace('pr_err("\\nError: Unable to read revid', 'printk("\\nError: Unable to read revid')
open(D + 'tfa_dsp.c', 'w', encoding='latin1', newline='').write(s)
print('revid', n)
