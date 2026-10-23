"""mkinit.py OLD_INIT_INC GENC1 GENC2055 OUT: build the g15c aw882xx_init.inc.h: per-pid blocks as direct stores into struct aw_device
(generated from the stock by gd.py --genc), stock-shaped 2013 efver check, 2055 two-phase block, stock dispatch."""
import re, sys

NL = chr(10)
old = open(sys.argv[1], encoding='utf-8', newline='').read().replace(chr(13) + chr(10), NL)
g1 = open(sys.argv[2], encoding='utf-8').read()
g55 = open(sys.argv[3], encoding='utf-8').read()


def between(txt, a, b):
    i = txt.index(a)
    j = txt.index(b, i) if b else len(txt)
    return txt[i:j]


header = old[:old.index('#include "aw882xx_log.h"') + len('#include "aw882xx_log.h"')] + NL + NL
h2113 = between(old, 'static int aw_pid_2113_frcset_check', 'static __always_inline int aw_pid_2113_dev_init')
h2308 = between(old, '/* 9bit signed int val */', 'static int aw_pid_2308_dev_init')
cmm = between(old, '#include <asm/unaligned.h>', 'noinline void aw_dev_init_cmm(struct aw_device *aw_dev);' + NL + 'int aw882xx_init')
tail_inc = '#include "g3k_aw_init.inc.h"'

secs = {}
for m in re.finditer(r'/\* ==== chip (0x[0-9a-f]+) ==== \*/' + NL + r'(.*?)(?=/\* ==== chip|\Z)', g1, re.S):
    secs[int(m.group(1), 16)] = [l for l in m.group(2).split(NL) if l.strip() and not l.startswith('UNKNOWN')]


def body(lines, calls):
    out = []
    ci = 0
    for l in lines:
        if l.strip().startswith('/* CALL'):
            if ci < len(calls):
                out.append(calls[ci])
            ci += 1
            continue
        out.append(l)
    return NL.join(out) + NL


def fn(name, chip, lines, calls=(), note=''):
    return ('/* g15c: stock block of chip 0x%x, stores generated from the disassembly (gd.py --genc) %s */' % (chip, note) + NL +
            'static __always_inline void %s(struct aw_device *aw_dev)' % name + NL + '{' + NL + body(lines, calls) + '}' + NL + NL)


out = header
# 2013 efver check as the stock
out += '''/*
 * g15c: aw_pid_2013_efver_check as the stock inlines it: the profile / boost registers are stored together with the block (before
 * the read), a part with EFVER L or H == 0 switches them off again (reg = 0xff); only EFVER A keeps them.
 */
static __always_inline void aw_pid_2013_efver_check(struct aw_device *aw_dev)
{
	unsigned int reg_val = 0;
	unsigned int efverh;
	unsigned int efverl;

	aw_dev->ops.aw_i2c_read(aw_dev->i2c, 0x7a, &reg_val);
	efverh = ((reg_val >> 13) & 7) ^ 7;
	efverl = ((reg_val & 0x1c00) >> 10) ^ 7;

	if (efverh && efverl) {
		aw_dev_info(aw_dev->dev, "A2013 EFVER A");
	} else {
		aw_dev->profctrl_desc.reg = 0xff;
		aw_dev->bstctrl_desc.reg = 0xff;
		if (efverh && !efverl)
			aw_dev_info(aw_dev->dev, "A2013 EFVER B");
		else
			aw_dev_info(aw_dev->dev, "unsupport A2013 EFVER");
	}
}

'''.replace(NL, NL)
out += fn('aw_pid_1852_dev_init', 0x1852, secs[0x1852])
out += fn('aw_pid_2013_dev_init', 0x2013, secs[0x2013], ['aw_pid_2013_efver_check(aw_dev);'])
out += fn('aw_pid_2032_dev_init', 0x2032, secs[0x2032])
out += fn('aw_pid_2071_dev_init', 0x2071, secs[0x2071])
out += h2113
out += fn('aw_pid_2113_dev_init', 0x2113, secs[0x2113],
          ['aw_dev->ops.aw_i2c_write(aw_dev->i2c, 0x73, 0x1d40);', 'usleep_range(3000, 3010);'])
out += h2308
out += fn('aw_pid_2308_dev_init', 0x2308, secs[0x2308])
out += fn('aw_pid_2116_dev_init', 0x2116, secs[0x2116])

# 2055: part 1 of the stock (before the version read) is the full 2055 block
p55 = [l for l in g55.split(NL) if l.strip() and not l.startswith('UNKNOWN') and not l.startswith('/* ====')]
k = [i for i, l in enumerate(p55) if l.strip().startswith('/* CALL')][0]
out += fn('aw_pid_2055_fill', 0x2055, p55[:k], note='(the 2055 stores; the stock repeats the block before and after the version read)')
out += '''/*
 * g15c: chip 0x2055: the stock stores the 2055 block, resets the chip (reg 0 = 0x55aa), reads the version (reg 0x23) and stores the block
 * AGAIN (the stores cannot be merged across the indirect calls; no version error in the stock: any value but 0x6708 is a plain 2055); 2055A then changes chip_id to 0x2055a and rewrites the monitor name,
 * work_mode.reg, voltage and temp registers (0xff). Common tail: usleep 2 ms, write 0x73 = 0x1d40, usleep 3 ms.
 */
static __always_inline void aw_pid_2055_dev_check(struct aw_device *aw_dev)
{
	unsigned int reg_data = 0;

	aw_pid_2055_fill(aw_dev);
	aw_dev->ops.aw_i2c_write(aw_dev->i2c, 0x0, 0x55aa);
	usleep_range(1000, 1100);
	aw_dev->ops.aw_i2c_read(aw_dev->i2c, 0x23, &reg_data);
	aw_pid_2055_fill(aw_dev);
	if (reg_data == 0x6708) {
		aw_dev->chip_id = 0x2055a;
		aw_dev->work_mode.reg = 0xff;
		memcpy(aw_dev->monitor_name, "aw882xx_pid_2055a_monitor.bin", 29);
		aw_dev->voltage_desc.reg = 0xff;
		aw_dev->temp_desc.reg = 0xff;
	}

	usleep_range(2000, 2010);
	aw_dev->ops.aw_i2c_write(aw_dev->i2c, 0x73, 0x1d40);
	usleep_range(3000, 3010);
}

'''
out += cmm + NL
out += '''noinline void aw_dev_init_cmm(struct aw_device *aw_dev);
int aw882xx_init(struct aw_device *aw_dev)
{
	int ret = 0;

	/* g3n: the stock calls aw_dev_init_cmm first */
	aw_dev_init_cmm(aw_dev);

	/* g15c: dispatch of the stock: 0x1852, 0x2013, 0x2032, 0x2055 (+A), 0x2071, 0x2113, 0x2116, 0x2308; every other id is rejected
	 * (0x2031, 0x2070 and 0x2115 are only the pivots of the stock's binary-search switch lowering, the earlier rounds took them for case values) */
	switch (aw_dev->chip_id) {
	case 0x1852:
		aw_pid_1852_dev_init(aw_dev);
		break;
	case 0x2013:
		aw_pid_2013_dev_init(aw_dev);
		break;
	case 0x2032:
		aw_pid_2032_dev_init(aw_dev);
		break;
	case 0x2055:
		aw_pid_2055_dev_check(aw_dev);
		break;
	case 0x2071:
		aw_pid_2071_dev_init(aw_dev);
		break;
	case 0x2113:
		aw_pid_2113_dev_init(aw_dev);
		break;
	case 0x2116:
		aw_pid_2116_dev_init(aw_dev);
		break;
	case 0x2308:
		aw_pid_2308_dev_init(aw_dev);
		break;
	default:
		aw_dev_err(aw_dev->dev, "unsupported chip id 0x%04x", aw_dev->chip_id);
		return -EINVAL;
	}

	if (ret)
		return ret;

	/* g3r: common tail of the stock: ef lock check, then the device probe */
	ret = aw882xx_dev_check_ef_lock(aw_dev);
	if (ret < 0) {
		aw_dev_err(aw_dev->dev, "ef has not been locked");
		return ret;
	}

	return aw882xx_dev_probe(aw_dev);
}

'''
out += tail_inc + NL
open(sys.argv[4], 'w', encoding='utf-8', newline='').write(out.replace(NL, chr(13) + chr(10)))
print('written', len(out))
