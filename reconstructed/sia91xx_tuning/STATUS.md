# sia91xx_tuning - HyperOS 3 name; in OS4 the module is `sia91xx_tuning_dlkm.ko`: EQUIVALENT vs OS4 is recorded in `work/re/src/sia91xx_tuning_dlkm/STATUS.md` (g5 confirmed 17.10.2026)

OS3 history of this directory: sia91xx_tuning - EQUIVALENT

---

# sia91xx_tuning - EQUIVALENT

`compare_ko.py`: function set (10), function bytes, relocations, globals, imports+CRCs, modinfo all OK.

Source basis: SI-IN `sia91xx_v3.1.0/sipa_tuning_if.c` + `sipa_tuning_misc.c` (OnePlus 13 modules tree,
`vendor/oplus/kernel/audio/codecs/sia91xx_v3.1.0/`), merged into one file (`sia91xx_tuning.c`).

Differences of the stock binary versus that public source (all reproduced):
- `myCircularQueueEnQueue` returns -1 (bool true) when `copy_from_user` fails (public code ignores the result).
- `myCircularQueueDeQueue` returns -1 (bool true) when `copy_to_user` fails and unlocks the mutex.
- log strings use `\n` instead of `\r\n` ("run !! \n", "wait reply event timeout !! \n", "kmalloc failed \n"; the kzalloc message
  text says "kmalloc failed"), `sipa_tuning_write` zero-initialises its 1 KB stack buffer.
- `DATA_MAX_LEN` = 1024, `MAX_SIZE` = 10, `sizeof(sipa_sync_t)` = 0x28a0 (struct mutex is 48 bytes in this GKI), `sizeof(sipa_turning_t)` = 0xa280
  (=> `kzalloc` becomes `kmalloc_large`).
- The `#if 0` firmware/power ioctls of the public tree are absent (`sipa_tuning_cmd_unlocked_ioctl` has no stack protector).

Build specifics needed for equivalence (see BUILD.bazel / source comments):
- `local_defines = ["CONFIG_LTO=1"]` (as for the other modules).
- `copts = ["-ffunction-sections", "-fdata-sections"]`: stock was linked per function section, so calls between local
  functions are R_AARCH64_CALL26 relocations (`.text+off`); without it the assembler resolves them and the function bytes differ.
- Lock keys: `mutex_init()` / `init_waitqueue_head()` each have a static zero-size `lock_class_key`; the stock LTO link merged them to one
  `.bss` address (all relocs resolve to `sipa_tuning_if_init.__key`). Our non-LTO link keeps `.bss.<sym>` sections separate, so the macros are
  redefined in the source to put every key in one explicit section (`.bss.sipa_lockkeys`). Codegen is unchanged.

## OS4 (17.10.2026)
Re-verified against the OS4 stock `sia91xx_tuning_dlkm.ko`: see `work/re/src/sia91xx_tuning_dlkm/STATUS.md` (same source, renamed to the OS4 module name; only the ThinLTO hash names needed updating for tas25xx).
