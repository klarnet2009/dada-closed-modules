D = '<workdir>/work/re/src/sia91xx_dlkm/'


def edit(fn, pairs):
    s = open(D + fn, encoding='latin1', newline='').read()
    for old, new, cnt in pairs:
        c = s.count(old)
        if c != cnt:
            print('COUNT', fn, c, cnt, repr(old[:70]))
            continue
        s = s.replace(old, new)
    open(D + fn, 'w', encoding='latin1', newline='').write(s)


edit('sia91xx_common.c', [
    ('#define LOG_FLAG\t"sia91xx_common"', '#define LOG_FLAG\t"sipa_91xx"', 1),
    ('Failed to read Revision register: %d \\r\\n"', 'Failed to read Revision register: %d \\n"', 1),
])
edit('sipa.c', [
    ('pr_debug("[debug][%s] %s: ucontrol = %ld, channel_num = %d \\n",', 'pr_debug("[debug][%s] %s: ucontrol = %ld, channle = %d \\n",', 1),
])
s = open(D + 'sipa_aux_dev_if.c', encoding='latin1', newline='').read()
i = s.index('int soc_codec_conf_sia91xx(') if 'int soc_codec_conf_sia91xx(' in s else -1
print('conf fn at', i)
j = s.index('soc_sia81xx_init ret', i)
s = s[:j] + 'soc_sia91xx_init ret' + s[j + len('soc_sia81xx_init ret'):]
open(D + 'sipa_aux_dev_if.c', 'w', encoding='latin1', newline='').write(s)
