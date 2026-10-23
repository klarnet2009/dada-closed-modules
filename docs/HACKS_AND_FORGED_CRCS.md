# Hacks, forged CRCs and stub providers

Some modules compare equal to the stock only because of a documented hack. None of these changes the code that runs in the function bodies unless stated; several change numbers that the kernel checks at load time. This page says what exists and where.

## Kinds

- **Forged export/import CRC.** Under `CONFIG_MODVERSIONS` every exported and imported symbol carries a CRC computed by genksyms from the prototype and all the types it reaches. When the real prototype or struct body is not known (the stock tree differs from ours, or a member is unknown), the CRC is made to match the stock in one of the ways below. A forged CRC makes the module load against the stock providers/consumers, but it proves nothing about the real prototype: if the forged declaration differs from the real one, the ABI would differ silently.
  - *GENKSYMS-only padding* (`tools/padcrc.py`, `tools/crcpad.py`): an enum value, attribute or struct member under `#ifdef __GENKSYMS__` with an identifier fitted until the CRC hits. Code and ABI are untouched. The padding must also be in the header prototype (genksyms takes the first declaration).
  - *`crcforge`*: a forged declaration used only for the CRC (for example `mi_power`: one import, `millet_binder`: two exports).
  - *`crc_patch.py` / `crc_patch2.py`*: the CRC words in the built `.ko` are overwritten with the stock values after the build (`tas25xx_dlkm`: 2 export CRCs; `minidump`: 1 import CRC; `tas25xx` in the first round). `crc_patch2.py` also handles import CRCs.
- **Stub providers** (`stubs/`, `tools/gen_stubs.py`): a module imports symbols of another module that is not part of the build. A stub module with the same exported names and exactly the stock CRCs (genksyms hashes `void sym ( struct tag { UNKNOWN } * )` with crc32 and a short struct tag is searched until the CRC matches) stands in so that the module under test links. **A matching import CRC through a stub does not confirm the prototype**; that is how one real prototype error in a charging module slipped through the old tool. The strict tool reports such imports as INFO. The stubs and their `os4_*`/`mca_st_*`/`*_stub` names appear in the built module's `depends=`; the package fixes that with `tools/modinfo_fix.py`. Stub trees in this repository: `stubs`, `stubs_a17A`, `stubs_a17B`, `stubs_a17C`, `stubs_a17D`, `stubs_g1`, `stubs_g13a`, `stubs_g13b`, `stubs_g13d_g2k`, `stubs_g13d_g5`, `stubs_g13g`, `stubs_g13l`, `stubs_g14e`, `stubs_g14e_a`, `stubs_g14e_b`, `stubs_g14f`, `stubs_g14g`, `stubs_g15c`, `stubs_g1e`, `stubs_g2g`, `stubs_g2h`, `stubs_g2i`, `stubs_g2j`, `stubs_g2k`, `stubs_g3`, `stubs_g3g`, `stubs_g3i`, `stubs_g3w`, `stubs_g4`, `stubs_g40g`, `stubs_g41a`, `stubs_g5`, `stubs_g53a`, `stubs_g6`, `stubs_mcaA`, `stubs_mcaB`, `stubs_misc`, `stubs_perfA`, `stubs_perfB`, `stubs_perfC`, `stubs_portA`, `stubs_portB`, `stubs_portB_g`, `stubs_portC`, `stubs_portD`. They are build helpers, never modules to load.
- **`#line` pins.** `__LINE__` is baked into log macros and bug-table entries; a `#line N` before a statement makes our object carry the stock line number. In the Wi-Fi drivers a generated pin file was used for all chips: this is **not** restored code, it only equalises numbers.
- **Codegen hints**: `likely`/`unlikely`, `__builtin_expect_with_probability`, `[[clang::__noinline__]]`/`[[clang::always_inline]]` on single calls, local copies of inline helpers from 6.6.57, dead literals, thin static helpers where LLVM folds a phi: only to make the compiler emit the stock instruction sequence.
- **Environment defines** in the Wi-Fi drivers (`__ANDROID_COMMON_KERNEL__`, `CONFIG_SCHED_WALT`, ...) derived from the stock function and import sets, not from the vendor Kconfig.
- **Guessed content**: names invented for unnamed vendor structures and functions (for example `MI_*` in the Wi-Fi extension file), meaning of hardware version numbers read from the disassembly, struct fields of the audio amplifier drivers. Each module's `STATUS.md` lists them.

## Known cases (checked against the notes of the modules)

| module | hack |
|---|---|
| `cs35l43_dlkm` | forged CRC of the stub provider for the import `cs_dsp_load_coeff` |
| `debug_ext`, `minidump` | import CRC of `qcom_is_dma_buf_file` via a forged struct tag in the stub provider (`minidump`: set after the build with `crc_patch2.py`) |
| `mi_power` | one import CRC forged (`crcforge`) |
| `mi_memory` | forged export CRCs of the `ufs_qcom` stub provider |
| `plat_common`, `rtg` | export CRCs forged (`plat_common`: 28 of 39 exports; `rtg`: a GENKSYMS-only extra parameter); the cause is a `task_struct` of another tree |
| `perf_actuator` | 2 export CRCs forged (GENKSYMS-only padding) |
| `millet_binder` | export CRCs of 2 symbols forged |
| `xiaomi_touch` | 2 export CRCs forged (GENKSYMS-only identifiers) |
| `fs19xx_dlkm` | forged member name for the `fsm_get_config` CRC |
| `tas25xx_dlkm` | 2 export CRCs set after the build (`crc_patch2.py`) |
| `sia91xx_dlkm` | the two exports `sipa_multi_channel_power_*` keep the stock CRC unreproduced (about 2000 declaration variants tried); the kCFI hashes of the same functions match |
| `mist`, `miloadtrace` | forged export/import CRCs from the first round (OS3) |
| `sia91xx_dlkm`, `tfa98xx_dlkm`, `aw882xx_dlkm` | not forged but not reproduced either: the import CRC of `set_smartpa_codec` (stock `0x77aedcd7`, ours `0xab30d685` in `tfa98xx_dlkm`) and the two `sipa_multi_channel_power_*` export CRCs stay different from the stock; these modules are COMPILES-only anyway |
| `xr_qi` | 10 of 14 export CRCs forged by GENKSYMS-only pads (`xr_qi` is community-only, PARTIAL) |
| `mca_smart_charge` | the export CRC of `mca_smartchg_if_ops_register` (stock `0x85e2f9d0`) is forged by a GENKSYMS-only pad: our own spelling of the struct gives the OS3 value `0xf068973d`, the OS4 member is unknown |
| `mitee_dlkm` | built only against forged `os4_*` stub providers (`gh_msgq`, `secure_buffer`, `qcom-scm`, `minidump`, `gh_rm_drv`) whose export CRCs were fitted; `depends=` therefore differs; the stub modules are **not published and must never be shipped** |
| `qca_cld3_*` (Wi-Fi) | the `#line` pin table (equalises line numbers only), the force-included `xm_compat_6657.h` (6.6.57 header emulation, not Xiaomi code), appended PCL table overrides, bug-compatible places; see `docs/WIFI.md` |
| `mca_*` (charging) | forged `os4_*` stub providers; shared BTF-derived header overlays `stubs_g13a`, `stubs_g13b`, `stubs_g13d_g2k`, `stubs_g13d_g5`, `stubs_g13g` (quick charge) and `stubs_g13l` (`bq27z561`) (headers only, no modules) |
| `bq27z561`, `mca_strategy_quickchg` | struct layouts rebuilt from the stock BTF and pinned by `_Static_assert` on sizes and offsets (`bq_fg_chip` 1856 bytes, `fuelguage_ic_ops` 616 bytes, the quick-charge `info` 3168 bytes); member names that the BTF stores as unnamed (`?strNNN`) are guessed from use; a few hardware thresholds are copied from stock immediates without a known meaning |

The generated table below lists every exported module whose `STATUS.md` mentions forged CRCs, GENKSYMS-only padding or a post-build CRC patch (a mention is not proof that the hack is still active: the note may say that it was removed). Stub providers, `#line` pins and codegen hints are too widespread to list; they are described in each `STATUS.md`.

| module | notes mention |
|---|---|
| cameralog | forged CRC |
| cameramsger | forged CRC |
| debug_ext | forged CRC |
| lb | forged CRC |
| mca_business_battery_comp | forged CRC |
| mca_business_charger_comp | forged CRC, genksyms-only padding |
| mca_qcom_subpmic_proxy | forged CRC |
| mca_strategy_fg_comp | forged CRC, genksyms-only padding |
| mi_damon | forged CRC |
| mibbr | forged CRC |
| miloadtrace | forged CRC, genksyms-only padding |
| mist | forged CRC, genksyms-only padding |
| process_monitor | forged CRC |
| tas25xx | crc_patch after build |
| aw882xx_dlkm | forged CRC, genksyms-only padding |
| binder_gki | forged CRC, genksyms-only padding |
| bq27z561 | forged CRC |
| charger_partition | genksyms-only padding |
| cs35l43_dlkm | forged CRC |
| dcvs_main | forged CRC, genksyms-only padding |
| dump_binder | forged CRC |
| dynamic_readahead | forged CRC, genksyms-only padding |
| fault_report | forged CRC |
| fs19xx_dlkm | forged CRC |
| matt | forged CRC, genksyms-only padding |
| mca_basic_wireless | forged CRC |
| mca_bmd | forged CRC, genksyms-only padding |
| mca_buckchg_jeita | forged CRC |
| mca_charge_mievent | forged CRC |
| mca_charger_thermal | forged CRC, genksyms-only padding |
| mca_connector_antiburn | forged CRC |
| mca_hwid | forged CRC |
| mca_path_control | forged CRC, genksyms-only padding |
| mca_platform_cp_class | forged CRC |
| mca_platform_fg_ic_ops | forged CRC |
| mca_protocol_class | genksyms-only padding |
| mca_protocol_pd_class | forged CRC, genksyms-only padding |
| mca_pwm_fan | forged CRC, genksyms-only padding |
| mca_quick_wireless | forged CRC |
| mca_smart_charge | forged CRC, genksyms-only padding |
| mca_strategy_buckchg | forged CRC |
| mca_strategy_fg_class | forged CRC |
| mca_strategy_quickchg | forged CRC |
| mca_vbat_ovp_monitor | forged CRC |
| mca_wireless_revchg | forged CRC |
| mi_fp | forged CRC, genksyms-only padding |
| mi_game | forged CRC |
| mi_low_lat_mem | forged CRC, genksyms-only padding |
| mi_memory | forged CRC |
| mi_power | forged CRC |
| mi_reclaim | forged CRC, genksyms-only padding |
| miev | forged CRC |
| millet_binder | forged CRC, genksyms-only padding |
| minet | forged CRC |
| miscore | forged CRC |
| mitee_dlkm | forged CRC |
| nuvolta_1652 | forged CRC |
| perf_actuator | forged CRC, genksyms-only padding |
| plat_common | genksyms-only padding |
| qca_cld3_kiwi_v2 | forged CRC |
| qca_cld3_peach | forged CRC |
| qca_cld3_peach_v2 | forged CRC |
| qca_cld3_qca6750 | forged CRC |
| qca_cld3_wcn7750 | forged CRC |
| rsmc_driver | forged CRC |
| rss_monitor | forged CRC |
| rtg | forged CRC, genksyms-only padding |
| sc8581_sy6541 | forged CRC, genksyms-only padding |
| sia91xx_dlkm | forged CRC, genksyms-only padding |
| swinfo | forged CRC |
| tango32 | forged CRC, genksyms-only padding |
| tas25xx_dlkm | crc_patch after build |
| tfa98xx_dlkm | forged CRC, genksyms-only padding |
| vip_sched | forged CRC |
| xiaomi_touch | forged CRC, genksyms-only padding |
| xr_qi | forged CRC, genksyms-only padding |
| xr_qs | forged CRC |

## Where a forgery was removed

- `dcvs_main`: the prototype `mi_recalculate_freq_mhz(..., s64)` was found from the kCFI hash; with it the export CRC matches without forging, and the forging hack was removed.
- `mi_low_lat_mem`, `xr_qi`, `mi_game`, `mi_damon`, `cameramsger`: wrong prototypes (not forged CRCs) were found from the kCFI hash with the substitution-aware search (`tools/kcfi/kcs.c`); the earlier search without Itanium substitutions missed every prototype that repeats a type, which is why some modules carried an "unnamed vendor type" for a while.
- `mca_strategy_buckchg` / `mca_smart_charge`: the member `int (*is_support_pps_100w)(void)` of the shared smart-charge class header was confirmed from the kCFI hash (`0x36b1c5a6`) and the stock function (global, with a preamble, returning int); with it the kCFI check of the buck-charging module went from 44 of 45 to 45 of 45. The export CRC of `mca_smartchg_if_ops_register` stays forged: the right prototype alone did not give `0x85e2f9d0`.
- `mca_business_charger_comp`, `mca_strategy_fg_comp`: enumerator names that were wrong (right values) were replaced by the stock BTF names (`docs/reviews/charging_enum_audit.md`); this removed no CRC forgery.
- The kCFI hash of an address-taken function carries no struct body or typedef name, so the same trick cannot remove a forged CRC that comes from a struct of another tree (6.6.57 versus 6.6.144) or from an unknown member (`cs_dsp`, `seq_file`, `task_struct`, `fs19xx`/`xiaomi_touch` members).
