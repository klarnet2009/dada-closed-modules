EQUIVALENT (reference = OS4 stock `vendor_ramdisk/miev.ko`)

Template: MiCode `piano-w-oss` `drivers/staging/miev/{midevice.c,mievent.c,*.h}` + `include/miev/mievent.h` (the XRING tree only has the public header; the same sources are in arctic-w-oss, erhu-w-oss, guitar-w-oss, tornado-v-oss, ...). The CSV entry "xring_dijun" is wrong for the .c files. `piano-w-oss` (loop index declared in the `for` statements) is the revision the stock was built from; `arctic-w-oss` differs only there and gives a 2-word scheduling difference in `init_module`.
Copies of the original files: `work/re/src/tplm/{arctic,piano}/` and server `<work-dir>`.

Changes relative to the template (OS4 differs from the template/HyperOS 3 in two functions):
- `cdev_tevent_alloc()`: when the json head snprintf fails it also calls `miev_release_work_msg(mievent->buf_ptr)` before returning NULL (leak fix of OS4).
- `miev_exit_module()`: new order `device_destroy, class_destroy, kfifo_free, cdev_del, unregister_chrdev_region, flush_workqueue, free works (msleep wait loop), destroy_workqueue, miev_workqueue = NULL, kfree(miev_dev)` (template freed `miev_dev` before the works).
- `miev_poll()`: local `miev_poll_wait()` = `poll_wait()` of the 6.6.57 headers (no `smp_mb()`), because the 6.6.144 `poll_wait()` has the extra `dmb ish`.
- ThinLTO lock-key hack `a17D_lockkeys.h` (mutex_init / init_waitqueue_head keys in one section), `CONFIG_MIEV=1` local define so that `include/miev/mievent.h` declares the exports instead of the empty inline stubs.

compare_ko vs OS4 stock: 20 functions byte-identical, relocations, objects, 5 exports + CRCs (`cdev_tevent_*`, struct `misight_mievent` CRCs reproduce natively), 43 imports + CRCs, modinfo (author huangqibo, `exception log transfer.`): all OK -> EQUIVALENT (~6 compile iterations, 1 bazel build).
Note: the `stubs_mcaA/miev_stub.c` (forged-CRC stub used by other ports) is no longer needed: this module exports the real CRCs.
