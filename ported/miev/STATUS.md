# miev - EQUIVALENT (strict compare_ko_v2: only metadata differences; the miev_works+720 finding was a compare artifact, see G9 note)

Old first line (before the strict recheck G9): EQUIVALENT (reference = OS4 stock `vendor_ramdisk/miev.ko`)

Template: MiCode `piano-w-oss` `drivers/staging/miev/{midevice.c,mievent.c,*.h}` + `include/miev/mievent.h` (the XRING tree only has the public header; the same sources are in arctic-w-oss, erhu-w-oss, guitar-w-oss, tornado-v-oss, ...). The CSV entry "xring_dijun" is wrong for the .c files. `piano-w-oss` (loop index declared in the `for` statements) is the revision the stock was built from; `arctic-w-oss` differs only there and gives a 2-word scheduling difference in `init_module`.
Copies of the original files: `work/re/src/tplm/{arctic,piano}/` and server `<work-dir>`.

Changes relative to the template (OS4 differs from the template/HyperOS 3 in two functions):
- `cdev_tevent_alloc()`: when the json head snprintf fails it also calls `miev_release_work_msg(mievent->buf_ptr)` before returning NULL (leak fix of OS4).
- `miev_exit_module()`: new order `device_destroy, class_destroy, kfifo_free, cdev_del, unregister_chrdev_region, flush_workqueue, free works (msleep wait loop), destroy_workqueue, miev_workqueue = NULL, kfree(miev_dev)` (template freed `miev_dev` before the works).
- `miev_poll()`: local `miev_poll_wait()` = `poll_wait()` of the 6.6.57 headers (no `smp_mb()`), because the 6.6.144 `poll_wait()` has the extra `dmb ish`.
- ThinLTO lock-key hack `a17D_lockkeys.h` (mutex_init / init_waitqueue_head keys in one section), `CONFIG_MIEV=1` local define so that `include/miev/mievent.h` declares the exports instead of the empty inline stubs.

compare_ko vs OS4 stock: 20 functions byte-identical, relocations, objects, 5 exports + CRCs (`cdev_tevent_*`, struct `misight_mievent` CRCs reproduce natively), 43 imports + CRCs, modinfo (author huangqibo, `exception log transfer.`): all OK -> EQUIVALENT (~6 compile iterations, 1 bazel build).
Note: the `stubs_mcaA/miev_stub.c` (forged-CRC stub used by other ports) is no longer needed: this module exports the real CRCs.

## G9 strict recheck (18.10.2026)
compare_ko_v2 reported miev_release_work_msg / write_kbuf referencing `obj:nwork_index` (stock) vs `bss-anon` (ours). Disassembly of the stock (`llvm-objdump -dr`) shows the stock relocation is ALSO `R_AARCH64_ADR_PREL_PG_HI21 miev_works+0x2d0` (symbol miev_works + 720 = `&miev_works[WORK_MAX_CNT]`, for the bound compare in check_work/release/write_kbuf and the pr_err argument); compare_ko_v2 resolved it by ADDRESS, and in the stock the merged `.bss` has nwork_index at 0x2e8 = end of the array, while our -fdata-sections `.bss.miev_works` section simply ends there. So source and relocations are identical; it was a false positive of the tool (not a real difference).
An experiment that took `&nwork_index` as the bound changed the code (3 functions differ) and was reverted: midevice.c is unchanged by G9.
compare_ko_v2.py was extended (G9): a relocation target that lies exactly at the END of a sized object now also carries the reading `end:<object>` (alternative reading, used only in function relocations); self-test v2_selftest.py still passes (SELFTEST OK).
Real Kleaf build now: compare_ko_v2 only buildmeta (intree/scmversion/staging vs built_with=DDK) fails; old compare_ko EQUIVALENT; kCFI preamble 20/20.
