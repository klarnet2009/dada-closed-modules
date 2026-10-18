# tas25xx_dlkm - EQUIVALENT (OS4 stock) with one documented hack: the 2 export CRCs are set by `crc_patch.py`

Reference: OS4 `<work-dir>` (vermagic 6.6.57-android15-8, modinfo `name=tas25xx_dlkm`; 17.10.2026).
This directory is the HyperOS 3 reconstruction `work/re/src/tas25xx/` (read its STATUS.md for the full story: TI UDA_0.0.21_7_K6.1 driver + Xiaomi receiver-switch pinctrl addition) renamed to the OS4 file name
(`ddk_module` / `out = tas25xx_dlkm.ko`, `tas25xx_dlkm_defconfig` ThinLTO).

Re-verification against the OS4 binary (real Kleaf build on 6.6.144, `compare_ko.py`):
- function set 117, function bytes all identical, relocations identical, globals identical, imports + CRCs (94) identical, modinfo identical (`name=tas25xx_dlkm`).
- The OS4 stock has the same code as HyperOS 3. The ONLY source change needed: the ThinLTO bitcode hash in the promoted-static names differs in OS4:
  `g_tas25xx.llvm.8547005323761844813`, `tas25xx_misc.llvm.8547005323761844813` (was ...10610663705188988610), `s_last_i2c_error.llvm.17590347322372196745` (was ...865695538464388113).
- Exports: 6 of 8 CRCs identical out of the box; `tas25xx_register_misc` (0x19deb7e1) and `tas25xx_deregister_misc` (0x7fa0526f) are identical to the HyperOS 3 values (the OS4 CRCs did not change) and still
  cannot be reproduced from source (the 5 Xiaomi-added members at the end of `struct tas25xx_priv` have unknown names; ~350M candidate spellings were tried earlier).
  HACK (documented): `python3 <build-dir> built.ko stock.ko out.ko` writes the two stock CRCs into `__kcrctab`; the code is untouched. After it: `compare_ko.py` => EQUIVALENT (verified 17.10.2026).
  Without the patch: DIFF only on those 2 export CRCs.
Build: `bash work/re/src/g3_tools/g3_build.sh tas25xx_dlkm`.
