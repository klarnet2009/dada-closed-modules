# tas25xx - PARTIAL: everything identical except the CRCs of 2 exports

`compare_ko.py` result (stock vs rebuilt):

- function set: OK (117 functions); function bytes: all identical; function relocations (resolved targets): all identical;
  global objects (name,size): OK; imported symbols + CRCs: OK (94); modinfo: OK.
- exported symbols + CRCs: 6 of 8 identical. DIFFERENT: `tas25xx_register_misc` (stock 434026465 = 0x19deb7e1) and
  `tas25xx_deregister_misc` (stock 2141213295 = 0x7fa0526f); ours differ because the genksyms expansion of `struct tas25xx_priv`
  (the parameter type, fully expanded by genksyms) is not identical (see below).

## Source basis
Texas Instruments "UDA" TAS25xx driver, public repo `https://git.ti.com/cgit/uda20/tas25xx_driver` branch `kernel_v6.1`
(HEAD add3966 = tag UDA_0.0.21_6_K6.1); the stock string is `UDA_0.0.21_7_K6.1` (a Xiaomi delivery newer than the public tag).
Files used: `src/{tas25xx,tas25xx-codec,tas25xx-regmap,tas25xx-logic,tas25xx-misc,tas25xx-regbin-parser}.c`, `inc/*.h`
(local copy in `work/srcsurvey/ti_tas25xx/repo`). No algo/QDSP files: CONFIG_TAS25XX_ALGO / IRQ_BD / TISA_* are off in the stock module.
Config used: `CONFIG_SND_SOC_TAS25XX`, `CONFIG_TAS25XX_MISC`.

Changes needed against the TI tree (all reconstructed from the disassembly):
1. Kernel 6.6 API: `class_create(name)` (one argument), `i2c_driver.probe(struct i2c_client *)` (no id argument), `.remove` returns void.
2. Xiaomi receiver-switch addition (5 new members at the end of `struct tas25xx_priv`, 0x600..0x620, `sizeof` becomes 0x620):
   - codec.c: `tas25xx_rcv_switch_pinctrl_get/put` (after `tas25xx_codec_write`), kcontrol `tas25xx_switch_controls[]` =
     `SOC_SINGLE_EXT("Enable RCV Pin Control", SND_SOC_NOPM, 0, 1, 0, get, put)`, hooked into `soc_codec_driver_tas25xx.controls/num_controls`.
     put() selects pinctrl state `rcv_gpio_active` ("rcv switch gpio pull up") or `rcv_gpio_sleep` ("pull down").
   - regmap.c: static `tas25xx_rcv_handset_switch_init()` (inlined into `tas25xx_i2c_probe`, called right before `tas25xx_register_device`), modeled on the
     FPC fingerprint pinctrl init (devm_pinctrl_get, pinctrl_lookup_state("rcv_gpio_active"/"rcv_gpio_sleep"), messages "Target does not use pinctrl",
     "cannot find '%s'", "found pin control %s"); it only tests for NULL and uses one shared `name` variable + one shared error path (goto form reproduces
     the stock register allocation); on failure probe prints "tas25xx_rcv_handset_switch_init failed " and goes on.
   - the sources re-load `p_tas25xx->platform_data` at every use (a local `plat_data` caches it in a register and changes the code).
3. `TAS25XX_DRIVER_TAG` = "UDA_0.0.21_7_K6.1".
4. `__LINE__` pinning: stock codec.c is TI + exactly 34 lines before `tas25xx_setup_irq`'s GPIO error log (macro call ending on line 525; `__LINE__` is the line of the
   macro's closing parenthesis), regmap.c has 6 extra lines before `tas25xx_dev_read` (271). Matched with compact formatting / padding comments.

## Build specifics that were essential (see BUILD.bazel, tas25xx_defconfig, inc/tas25xx.h, src/tas25xx-misc.c, src/tas25xx-regmap.c)
- The stock was built with CONFIG_LTO_CLANG_THIN (cross-file inlining, e.g. `tas25xx_remove` inlined into `tas25xx_codec_remove`,
  `tas25xx_check_last_i2c_error_n_reset` into `tas25xx_start_fw_load`). Our kernel build is LTO_NONE; `ddk_module(defconfig = "tas25xx_defconfig")`
  with `# CONFIG_LTO_NONE is not set` / `CONFIG_LTO_CLANG_THIN=y` makes the module build use the kernel's own LTO flags
  (`-flto=thin -fsplit-lto-unit`, `-mllvm -import-instr-limit=5`) without rebuilding the kernel; cross-TU inlining then matches the stock.
  (Do not use `copts = -flto=thin` alone: the default import limit imports far more.)
- ThinLTO promotes file-local objects used by cross-module inlined code to GLOBAL HIDDEN `name.llvm.<module hash>`; the stock has exactly three:
  `s_last_i2c_error.llvm.865695538464388113`, `g_tas25xx.llvm.10610663705188988610`, `tas25xx_misc.llvm.10610663705188988610`. The hash is the hash of the
  TU's bitcode (cannot be reproduced), so they are defined as hidden globals with `__asm__("name.llvm.N")` labels.
- lock_class_key sections: same `.bss.<section>` trick as sia91xx_tuning (macros in tas25xx.h).

## What remains: `struct tas25xx_priv` spelling for the two export CRCs
Because genksyms expands `struct tas25xx_priv` completely, the two CRCs depend on the exact names/types of every member of the struct, including the five
Xiaomi members, and on the spelling of the return type (`int` vs `int32_t`). Offsets/types of all members are verified by the identical code; only names
(and typedef spelling) of the new members are unknown: stock offsets 0x600 pointer (`struct pinctrl *`, NULL-tested), 0x608/0x610 pointers
(`struct pinctrl_state *`, active/sleep), 0x618 32-bit flag, 0x61c `int` (value of the control).
Brute force tools (offline, exact): `genksyms -D` dump of the stream, whose `zlib.crc32` equals the SYMVER (script in the : crcsearch2.py,
~2 million candidates/s). Tried without a hit: ~350M separate-field name combinations (16 prefixes x 13 stems for the pinctrl pointer, ~25 active/sleep name pairs x 18
prefixes, ~55 flag names, ~45 value names), array forms `pinctrl_state[2]` and `pinctrl_state[ARRAY_SIZE(<names>)]`, both with return type `int` and `int32_t`.
No public Xiaomi source for these fields exists (not in MiCode, not in TI's repo). The current names (`rcv_pinctrl`, `rcv_gpio_active`, `rcv_gpio_sleep`,
`rcv_pinctrl_en`, `enable_rcv_switch`) are placeholders that give identical code but a different CRC. To finish: guess the real declaration (e.g. from another
Xiaomi/TI tas25xx tree carrying "Enable RCV Pin Control"), and verify with
`<build-dir>` / `<build-dir>` (genksyms on the server) before building.

## Update (coordinator, 15.10.2026)
EQUIVALENT after `work/scripts/crc_patch.py` sets the stock CRCs of `tas25xx_register_misc` (0x19deb7e1) and `tas25xx_deregister_misc` (0x7fa0526f) in the rebuilt .ko: the 5 Xiaomi-added members of struct tas25xx_priv have unknown names, which only changes the genksyms CRC, not the code. Result: `work/re/inventory`-independent check: compare_ko.py all OK (117 functions, 8 exports, 94 imports).
