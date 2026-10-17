# millet_core - EQUIVALENT vs OS4

G6 re-check against the HyperOS 4.0 stock (17.10.2026): OS3 -> OS4 stock changed 15 of 14 functions (13 new): the core-uid record atomic_t core_uid_rec[74] (and its export) is gone, replaced by two identical 64-bucket uid hash tables (core_uid_hashtable / uid_rec_hashtable, each with a static spinlock, struct {uid_t uid; struct hlist_node node;} nodes kmalloc'ed GFP_KERNEL, hash_32 key) with the functions {core_uid,uid_rec}_hash_{add,del,find,find_and_clear,clear,show} (11 new exports; core_uid_hash_find_and_clear is global but not exported, as in stock; ksymtab order = stock); millet_sock_show prints core_uid_hash_show(m, ",") and "\nuid_rec:\n" + uid_rec_hash_show(m, "\t") instead of the core_uid_rec loop; millet_init hash_init()s both tables first; a module_exit (cleanup_module: remove_proc_entry x3, netlink_kernel_release, uid_rec_hash_clear, core_uid_hash_clear) and MODULE_DESCRIPTION("Millet smart frozen control subsystem") were added. Recovered from the OS4 disassembly (no template): the 12 functions are written once as a macro MILLET_UID_HASH(pfx); the show function needs 'int cnt' + 'cnt < ARRAY_SIZE(uids)' to reproduce the widened-induction-variable codegen. Real Kleaf build vs OS4 stock vendor_ramdisk/millet_core.ko: compare_ko.py EQUIVALENT (27 functions, 23 exports with CRC, 36 imports); data objects (g6/datacmp.py): identical. The OS3 source is kept as g6/millet_core_os3_backup.c (the OS3 binary needs it). NOTE: millet_pkg (which used core_uid_rec) must be rebuilt together, see its STATUS.

Earlier headline (HyperOS 3 round): millet_core - EQUIVALENT


Template: XRING `dijun-v-oss` branch `xring-modules/drivers/mihw/millet/millet_core.c` + `millet.h` + `uapi/millet.h` (fetched from `<build-dir>`, copy in `work/re/ref/portD/dijun_millet/`).
Builds against 6.6.144 unchanged (ThinLTO `millet_core_defconfig`, `CONFIG_LTO=1`, function/data sections).

compare_ko: 14 functions byte-identical, relocations identical, global objects, 13 exports + CRCs, 30 imports + CRCs, modinfo: all OK -> EQUIVALENT.

Changes relative to the template (the stock is a newer revision than the XRING one):
- `NETLINK_MILLET` is 31 (template 30).
- new exported globals: `atomic_t core_uid_rec[74]`, `atomic_t core_black_pid_rec[10]`, `atomic_t core_black_uid_rec[10]`, `int freezebinder_enable` (used by millet_pkg/millet_binder).
- `dump_send_msg()` also prints `"up sig: 0x%lu\n", msg->pri[0]` when `owner == SIG_TYPE`.
- `millet_sock_show()` appends the three `core_*_rec` tables (`"core_uid_rec:\n"`, `"%d,"`, ...; the value is read once into a local).
- `millet_sendto_user()`: the `has_init` / `loop_init` failure paths return without `kfree_skb()` (the stock leaks the skb there; reproduced).
- `judge_millet_freeze_switch`, `millet_v`, `millet_rootdir` are global but not exported (as in stock).
