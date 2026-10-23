D = '<workdir>/work/re/src/tfa98xx_dlkm/'


def edit(fn, pairs):
    s = open(D + fn, encoding='latin1', newline='').read()
    crlf = '\r\n' in s
    for old, new in pairs:
        if crlf:
            old = old.replace('\n', '\r\n'); new = new.replace('\n', '\r\n')
        if s.count(old) != 1:
            print('NOT UNIQUE/FOUND', fn, s.count(old), repr(old[:70]))
            continue
        s = s.replace(old, new)
    open(D + fn, 'w', encoding='latin1', newline='').write(s)


W = 'Warning: Optimal settings not found for device with revid = 0x%x \\n", tfa->rev);'
edit('tfa_init.c', [
    # 9888 / 9912 default message (stock prints it, without the leading newline)
    ('\t\ttfa_reg_write(tfa, 0x83, 0x0014); //POR=0x0013\n\t\t/* ----- generated code end   ----- */\n\t}\n\n\tpatch_version',
     '\t\ttfa_reg_write(tfa, 0x83, 0x0014); //POR=0x0013\n\t\t/* ----- generated code end   ----- */\n\t} else {\n\t\tpr_info("' + W + '\n\t}\n\n\tpatch_version'),
    ('\t\ttfa9912_faim_protect(tfa, 0);\n\t}\n\n\treturn error;',
     '\t\ttfa9912_faim_protect(tfa, 0);\n\t} else {\n\t\tpr_info("' + W + '\n\t}\n\n\treturn error;'),
    # 9873 / 9874: interrupt_enable[0] = 0x11e + tfa_irq_init after the switch
    ('\t\tpr_info("\\nWarning: Optimal settings not found for device with revid = 0x%x \\n", tfa->rev);\n\t\tbreak;\n\t}\n\terror = tfa_set_bf_volatile(tfa, TFA9873_BF_FSSYNCEN, 0);',
     '\t\tpr_info("\\nWarning: Optimal settings not found for device with revid = 0x%x \\n", tfa->rev);\n\t\tbreak;\n\t}\n\ttfa->interrupt_enable[0] = 0x011e;\n\ttfa_irq_init(tfa);\n\terror = tfa_set_bf_volatile(tfa, TFA9873_BF_FSSYNCEN, 0);'),
    ('\t\tpr_info("\\nWarning: Optimal settings not found for device with revid = 0x%x \\n",\n\t\t\ttfa->rev);\n\t\tbreak;\n\t}\n\n\treturn error;\n}\n\nvoid tfa9874_ops',
     '\t\tpr_info("\\nWarning: Optimal settings not found for device with revid = 0x%x \\n",\n\t\t\ttfa->rev);\n\t\tbreak;\n\t}\n\n\ttfa->interrupt_enable[0] = 0x011e;\n\ttfa_irq_init(tfa);\n\n\treturn error;\n}\n\nvoid tfa9874_ops'),
])
edit('g3k_chips.inc.h', [
    # 9865: stock writes reg 0xd2 = 0 before 0xda = 2
    ('\t\ttfa_reg_write(tfa, 0x75, 0x9900);\n\t\ttfa_reg_write(tfa, 0xda, 0x0002);', '\t\ttfa_reg_write(tfa, 0x75, 0x9900);\n\t\ttfa_reg_write(tfa, 0xd2, 0x0000);\n\t\ttfa_reg_write(tfa, 0xda, 0x0002);'),
    # 9878: no 0x83 write in case 0x0a78; common tail sets interrupt_enable[0] and calls tfa_irq_init
    ('\t\ttfa_reg_write(tfa, 0x71, 0x303e);\n\t\ttfa_reg_write(tfa, 0x83, 0x009a);\n\t\tbreak;\n\tcase 0x1a78:', '\t\ttfa_reg_write(tfa, 0x71, 0x303e);\n\t\tbreak;\n\tcase 0x1a78:'),
    ('\t\ttfa_reg_write(tfa, 0xd5, 0x0000);\n\t\ttfa_irq_init(tfa);\n\t\tbreak;\n\tdefault:\n\t\tpr_info("\\nWarning: Optimal settings not found for device with revid = 0x%x \\n", tfa->rev);\n\t\tbreak;\n\t}\n\n\treturn error;\n}\n\nvoid tfa9878_ops',
     '\t\ttfa_reg_write(tfa, 0xd5, 0x0000);\n\t\tbreak;\n\tdefault:\n\t\tpr_info("\\nWarning: Optimal settings not found for device with revid = 0x%x \\n", tfa->rev);\n\t\tbreak;\n\t}\n\n\ttfa->interrupt_enable[0] = 0x011e;\n\ttfa_irq_init(tfa);\n\n\treturn error;\n}\n\nvoid tfa9878_ops'),
])
