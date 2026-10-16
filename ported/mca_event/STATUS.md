# mca_event - EQUIVALENT (agent mcaA, port task)

Template: XRING tree work/re/xiaomi-modules/mca/mca_common/mca_event.c (builds unchanged on 6.6.144)
Result: EQUIVALENT: compiles on 6.6.144; function set (7), bytes, resolved relocs, exports + CRC (4), imports + CRC (20), modinfo (depends mca_sysfs), 8 data objects identical

- stock is the same version as the template; only header facts needed: MCA_EVENT_TYPE_END = 10 (g_mca_event_bnh is 10*72 bytes), MCA_EVENT_WR_BUF_SIZE = 128
- lock keys: include mca/common/mca_lockkey.h first (all keys in one .bss.mca_lockkeys section, like the stock LTO link); in addition mca_event_sysfs_attrs is placed in the same section because in the stock .bss the zero-size keys share its address and compare_ko resolves relocations to it as the key symbol
- provider stub: mca_sysfs_stub (new in stubs_mcaA)
