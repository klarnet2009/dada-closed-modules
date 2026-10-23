import re
p = '<workdir>/work/re/src/sia91xx_dlkm/sipa_regmap.c'
raw = open(p, encoding='latin1', newline='').read()
crlf = '\r\n' in raw
s = raw.replace('\r\n', '\n')

# A: helper messages carry the name of the stock function
s = s.replace('LOG_FLAG, __func__, list->node_size, sizeof(SIPA_REG_PROC));', 'LOG_FLAG, "sipa_regmap_proc_1_reg_list", list->node_size, sizeof(SIPA_REG_PROC));')
s = s.replace('LOG_FLAG, __func__, ret, si_pa->chip_type, si_pa->regmap, regs[i].addr);', 'LOG_FLAG, "sipa_regmap_proc_1_reg_list", ret, si_pa->chip_type, si_pa->regmap, regs[i].addr);')

# B: verify_chip_type as macros (caller __func__)
a = s.index('static __always_inline int verify_chip_type(')
b = s.index('struct regmap *sipa_regmap_init(')
new = '''#define verify_chip_type(ch, type) ({ \\
	int g41a_r = 0; \\
	if ((type) >= ARRAY_SIZE(reg_map_info_table)) { \\
		pr_err("[  err][%s] %s: chip_type(%u) is err \\n", LOG_FLAG, __func__, (type)); \\
		g41a_r = -ENODEV; \\
	} \\
	g41a_r; })
/* G41a: stock init / check_trimming / set_pvdd_limit range-check without a message */
#define verify_chip_type_q(ch, type) (((type) >= ARRAY_SIZE(reg_map_info_table)) ? -ENODEV : 0)

'''
s = s[:a] + new + s[b:]

# C: quiet variants in init, set_pvdd_limit, check_trimming
for fn in ('struct regmap *sipa_regmap_init(', 'void sipa_regmap_set_pvdd_limit(', 'void sipa_regmap_check_trimming('):
    i = s.index(fn)
    j = s.index('verify_chip_type(', i)
    assert j - i < 600, fn
    s = s[:j] + 'verify_chip_type_q(' + s[j + len('verify_chip_type('):]

# D: check_chip_id NULL regmap message
old = '\tif (NULL == regmap)\n\t\treturn -EPERM;\n\n\tif (0 != verify_chip_type(ch, chip_type))\n\t\treturn -EPERM;\n'
assert s.count(old) == 1
s = s.replace(old, '\tif (NULL == regmap) {\n\t\tpr_err("[  err][%s] %s: NULL == regmap \\n", LOG_FLAG, __func__);\n\t\treturn -EPERM;\n\t}\n\n\tif (0 != verify_chip_type(ch, chip_type))\n\t\treturn -EPERM;\n')

# E: si_pa is null messages in set_chip_on/off
old = '\tif (NULL == si_pa)\n\t\treturn false;\n\n\tif (0 != verify_chip_type(si_pa->channel_num, si_pa->chip_type))\n\t\treturn false;\n'
print('E count', s.count(old))
new = '\tif (NULL == si_pa) {\n\t\tpr_err("[  err][%s] %s: si_pa is null \\n", LOG_FLAG, __func__);\n\t\treturn false;\n\t}\n\n\tif (0 != verify_chip_type(si_pa->channel_num, si_pa->chip_type))\n\t\treturn false;\n'
s = s.replace(old, new)
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'w', encoding='latin1', newline='').write(s)
