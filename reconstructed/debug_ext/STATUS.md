# debug_ext - EQUIVALENT vs the OS4 (HyperOS 4.0) stock binary (17.10.2026; earlier rounds: perfB)

`compare_ko.py` against `<work-dir>`, real Kleaf ThinLTO build (`debug_ext_defconfig`): function set (28), function bytes, relocations, global objects,
exports (none), imports + CRCs (52) and modinfo all OK -> EQUIVALENT.
Documented hack: the import CRC of `qcom_is_dma_buf_file` (stock 0x9bf72b1a, built on 6.6.57 headers) cannot be produced by a real prototype against the 6.6.144 headers, so the provider stub
`work/re/src/stubs_g4/qcom_dma_heaps_stub.c` declares it as `void qcom_is_dma_buf_file(struct fgP1CcKW *)` with a forged struct tag (generator `work/a17/portB/gen_stubs.py`, genksyms emulation).
The stub is only a link-time stand-in for modpost; debug_ext.c itself uses the real prototype from `linux/qcom_dma_heap.h`.
(Older text below describes the HyperOS 3 round.)

`compare_ko.py` (ThinLTO build, `debug_ext_defconfig`): function set (28) OK, function bytes OK, relocations OK, global objects OK, exports (none) OK, modinfo OK.
Data objects (`dma_buf_procs_proc_ops`, `reboot/shutdown/customized_panic_ops`, `panic_trigger_nb`, `dma_buf_procs_size`, `__param_initrd_size`, `dma_buf_hash` ...): bytes + relocations identical
(`work/scripts/perfB_objcmp.py`); only the `__UNIQUE_ID___addressable_*` counter suffix differs.

## What still differs

Import CRC of `qcom_is_dma_buf_file`: stock `0x1c02c348`, our stub `0xa7b7c305`. Environmental: the provider stub is built against the 6.6.144 kernel headers while the stock `qcom_dma_heaps.ko`
was built against 6.6.57; its other export `qcom_store_dma_buf_fops` (`void (struct file *)`) mismatches the same way, so it is not a prototype problem (`bool qcom_is_dma_buf_file(struct file *file)` from
`linux/qcom_dma_heap.h`; spellings bool/int/const/forward-declared tried with the genksyms emulator, none gives the stock value). All kernel imports (`fput`, `iterate_fd`, `proc_*` ...) match.

## Source basis / lessons

- dma-buf per-process statistics are Qualcomm `drivers/soc/qcom/minidump_memory.c` (`md_dma_buf_procs`, `get_dma_info`, `struct priv_buf`, `dma_buf_priv`, jhash seed
  `0x9747b28c`) with Xiaomi changes: hash table 1<<13, extra `fd`/`exp_name` columns, `dma_buf_procs_sort` (per-process list sorted by size, `GFP_ATOMIC` nodes under `tasklist_lock`).
- `reserved_memory_stat` / `ko_memory_stat` (`debug_symbol_lookup_name("modules")` + `mod->mem[MOD_*]`; the "total" field is really `mem[INIT_DATA]+mem[INIT_RODATA]` in raw bytes) and
  `panic_trigger` (proc entry modes are literally `0x666`/`0x666`/`0222`) are Xiaomi originals.
- ThinLTO (`defconfig`) fixed the last function (`get_dma_buf_procs`): with plain `--lto=none` the peeled first loop iteration had a different branch order (17 source variants tried in `perfB_lab`).
- `_copy_from_user` is the inlined arm64 uaccess copy compiled with the 6.6.57 `__access_ok()` (`addr <= limit - size`, no `size <= limit` check, added in later 6.6 stable). Reproduced by
  defining `__access_ok` before including the kernel headers (`#define __access_ok __access_ok` + own inline) - generic trick for any module whose uaccess code differs only there.
- Register-allocation mismatches can be searched mechanically: `work/re/src/perfB_lab/gen_lab.py` builds N variants (here: all 120 declaration orders of the five accumulators of
  `reserved_memory_stat_show`) as extra global functions in one module and `work/scripts/perfB_labcmp.py` ranks them by disassembly diff against the stock function (found: declare `count` first).
- `#line N` before a `pr_err(..., __LINE__)` pins `__LINE__` (288/296 in `init_dma_buf_debug`, 132/140/148 in `panic_trigger_init`, 32/46/53 in the init function).
- `__func__` strings reveal static function names: the init function is `debug_ext_init`.

Stubs (`work/re/src/stubs_perfB`): `qcom_dma_heaps.ko` (`qcom_is_dma_buf_file`), `debug_symbol.ko` (`debug_symbol_lookup_name`).
