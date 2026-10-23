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


edit('tfa98xx.c', [
    ('\tmutex_lock(&tfa98xx->dsp_lock);\n\tif (tfa98xx->tfa->is_probus_device) {\n#ifdef CONFIG_MTK_PLATFORM\n\t\tuint32_t DataLength = 0;\n\n\t\terror = tfa98xx_receive_data_from_dsp(\n\t\t\tbuffer, count, &DataLength);\n#else\n\t\terror = send_tfa_cal_apr(buffer, count, true);\n#endif\n\t} else\n\t\terror = tfa_dsp_msg_read(tfa98xx->tfa, count, buffer);\n',
     '\tmutex_lock(&tfa98xx->dsp_lock);\n\t/* G41a: the stock has no probus branch here */\n\terror = tfa_dsp_msg_read(tfa98xx->tfa, count, buffer);\n'),
    ('\tif (tfa98xx->tfa->is_probus_device) {\n\n\t\tmutex_lock(&tfa98xx->dsp_lock);\n#ifdef CONFIG_MTK_PLATFORM\n\t\terror = tfa98xx_send_data_to_dsp(msg_file->data,\n\t\t\tmsg_file->size);\n#else\n\t\terr = send_tfa_cal_apr(msg_file->data,\n\t\t\t\t\t\t\t\tmsg_file->size,\n\t\t\t\t\t\t\t\tfalse);\n#endif\n\t\tmdelay(5);\n\n\t\tmutex_unlock(&tfa98xx->dsp_lock);\n\t} else {\n\t\tmutex_lock(&tfa98xx->dsp_lock);\n',
     '\t{\t/* G41a: the stock has no probus branch here */\n\t\tmutex_lock(&tfa98xx->dsp_lock);\n'),
])
edit('g3k_drv.inc.h', [
    ('\tint value;\n\n\tmutex_lock(&tfa98xx->dsp_lock);\n\tswitch (what) {\n\tcase 0: value = tfa_dev_mtp_get',
     '\tint value;\n\n\t/* G41a: stock pga_gain_get: no lock, no error check, (u32) value */\n\tif (what == 3) {\n\t\t*val = (u32)tfa_get_pga_gain(tfa98xx->tfa);\n\t\treturn 0;\n\t}\n\tmutex_lock(&tfa98xx->dsp_lock);\n\tswitch (what) {\n\tcase 0: value = tfa_dev_mtp_get'),
    ('\tmutex_unlock(&tfa98xx->dsp_lock);\n\tif (value < 0)\n\t\treturn -EIO;\n\t*val = value;\n\treturn 0;',
     '\tmutex_unlock(&tfa98xx->dsp_lock);\n\tif (what == 2) {\t/* G41a: stock temp_get sign-extends the s16 and never fails */\n\t\t*val = (u64)(long)(short)value;\n\t\treturn 0;\n\t}\n\tif (value < 0)\n\t\treturn -EIO;\n\t*val = value;\n\treturn 0;'),
    ('\tint err;\n\n\tif (what < 2 && val > 1)\n\t\treturn -EINVAL;\n',
     '\tint err;\n\n\tif (what == 3) {\t/* G41a: stock pga_gain_set: val > 7 -> -EINVAL, a failing set -> -EINVAL, no lock */\n\t\tif (val & ~7ULL)\n\t\t\treturn -EINVAL;\n\t\terr = tfa_set_pga_gain(tfa98xx->tfa, val);\n\t\treturn err < 0 ? -EINVAL : 0;\n\t}\n\tif (what < 2 && val > 1)\n\t\treturn -EINVAL;\n'),
])
