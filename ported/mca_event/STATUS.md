# mca_event - EQUIVALENT vs OS4

G6 re-check against the HyperOS 4.0 stock (17.10.2026): OS4 stock adds the export `void mca_event_report_multiple_uevent(const struct mca_event_notify_data *n_data, int count)` (kcalloc(count, 500) text buffer + kcalloc(count+1, 8) envp array, per element checks 'event is null' / 'event_len is invalid', memcpy, one kobject_uevent_env(KOBJ_CHANGE) for all, kfree both) and the notify buffer of mca_event_report_uevent grew from 128 to 500 bytes (MCA_EVENT_NOTIFY_SIZE redefined to 500 in mca_event.c, the shared stubs header keeps the OS3 value; prototype of the new function is local to the .c). Loop index is size_t (64-bit offset arithmetic, as in stock). Imports gain __kmalloc and memset (CRCs match). Real Kleaf build vs OS4 stock vendor_ramdisk/mca_event.ko: compare_ko.py EQUIVALENT (8 functions, 5 exports, 22 imports). Community-only source (charging stack): never flash it, never put it into a ROM build.

Earlier headline (HyperOS 3 round): mca_event - EQUIVALENT (port task)


Template: XRING tree work/re/xiaomi-modules/mca/mca_common/mca_event.c (builds unchanged on 6.6.144)
Result: EQUIVALENT: compiles on 6.6.144; function set (7), bytes, resolved relocs, exports + CRC (4), imports + CRC (20), modinfo (depends mca_sysfs), 8 data objects identical

- stock is the same version as the template; only header facts needed: MCA_EVENT_TYPE_END = 10 (g_mca_event_bnh is 10*72 bytes), MCA_EVENT_WR_BUF_SIZE = 128
- lock keys: include mca/common/mca_lockkey.h first (all keys in one .bss.mca_lockkeys section, like the stock LTO link); in addition mca_event_sysfs_attrs is placed in the same section because in the stock .bss the zero-size keys share its address and compare_ko resolves relocations to it as the key symbol
- provider stub: mca_sysfs_stub (new in stubs_mcaA)
