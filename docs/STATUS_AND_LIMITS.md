# Status and limits: what is proven here and what is not

State of 18.10.2026. Everything below is static verification of binaries on a build machine. **No module in this repository has been loaded or run on a phone.**

## 1. Modules per status

145 modules are tracked (`reconstructed/`, `ported/` and the notes in `docs/blocked/`):

| status | modules | meaning |
|---|---|---|
| EQUIVALENT | 100 | every function, relocation, object, export and import (with CRC) and the modinfo match the stock module |
| PARTIAL | 35 | interfaces and logic as far as checked, but some functions differ (layout and register allocation in most, real code differences in some: see the `STATUS.md` of the module) |
| COMPILES-only | 3 | builds, function set equals the stock one, but many bodies differ |
| BLOCKED | 6 | does not compile (missing definitions that exist in no public tree) |
| not started | 1 | analysis only: `mitee_dlkm` |

(3 modules appear twice, once under their HyperOS 3 name and once under their HyperOS 4 name: `tas25xx`, `typec_analog_acc`, `sia91xx_tuning`.)

Of the 100 EQUIVALENT modules, 99 passed the strict check described below; the rest (1) was compared only with the first-generation tool. The per-module value is in the last column of the tables in the README.

A module is only called EQUIVALENT when the check of the day found nothing. Twice in this project the check was found to have holes and modules had to be downgraded or fixed; see section 2 and `docs/reviews/`.

## 2. The old tool has holes, the strict tool closes most of them

`tools/compare_ko.py` was the first equivalence check. It compares the bytes of each function, relocations, the names and sizes of global objects, exports and imports with CRCs and the modinfo strings. It does **not**:

- compare relocations of functions whose bytes differ;
- compare the **content** of objects (`.data`, `.rodata`, `.bss`, tables, `__param`, `__trace_printk_fmt`) or static variables, only name and size of globals;
- compare anonymous `.rodata` other than as a C string up to the first NUL;
- look at `__bug_table`, `__jump_table`, alternatives;
- look at the **kCFI type hash**, the 4-byte word that sits just before each function symbol. A wrong hash does not change a single byte inside the function, but the kernel traps at the first indirect call to it.
- look at the `depends=` line of the modinfo (depmod and modprobe use it);
- tell a real prototype from a forged one: when a stub provider exposes the stock CRC, a matching import CRC proves nothing about the prototype.

`tools/compare_ko_v2.py` checks all of the above except the last point (it reports forged-provider imports as INFO), plus the binding of every function, code that belongs to no function, and the kernel version magic by flags. Its self-test (`tools/review/v2_selftest.py`) injects 17 kinds of corruption and catches all of them. `tools/preamble_cmp.py` and `tools/verify_pkg.py` check the kCFI hashes against the stock module and against the hashes the kernel expects (read from `vmlinux`).

What happened when the strict tool was run over the 100 older EQUIVALENT claims: 8 were really different (data tables shorter than stock, wrong DAI names, default thresholds, `__param` permissions, kCFI preamble words). Seven were fixed, one (`minet`) is still PARTIAL: two functions carry an extra kCFI word that cannot be removed without changing the inlining. Before the fix, our own `binder_gki` build had passed the old tool (9 of 10 functions) and still carried a wrong kCFI hash in the probe that would have trapped on the first binder allocation.

What the strict tool still does not check: `.eh_frame` and `.BTF`; memory accesses without relocations; `__FILE__` in the bug table is compared by basename; trailing zeros of anonymous pieces are ignored. Metadata always differs and is accepted: `intree=Y`, `scmversion=`, `srcversion=`, `staging=Y` versus `built_with=DDK`, the LTO `.note.Linux`, and `depends=` where a build named stub providers.

## 3. What a multiset diff proves, and what it does not

For modules that are not byte-identical (every PARTIAL module and the charging reviews) we compared, per function, the **multiset of instructions with registers masked**: stack accesses, frame setup, register moves, unconditional jumps and `ret` are dropped; `ldp`/`stp` are split; comparisons are normalised. Functions of the same module that were inlined in one build and not in the other are expanded before comparing. The result per function is one of LAYOUT (multisets equal), INLINE (equal after expansion), SELECT (only ALU/branch instructions differ), CONST (immediates differ too) and REAL (loads, stores, calls or traps differ). See `docs/reviews/multiset_audit.md` (part of it is in Russian).

It proves, when the multisets are equal: the function performs the same memory accesses (opcode, width, offset), calls the same functions the same number of times, contains the same constants and the same trap instructions (`brk`, from `BUG_ON`, fortify, bounds checks). That is what found a self-deadlock (`getsockopt` took a lock twice), wrong kCFI prototypes that would trap on the first call, a missing SACK loop that the sanitizer had replaced by `brk`, and several fields read from the wrong source.

It does **not** prove equivalence: operations may be permuted; an inverted branch with a mirrored layout has the same multiset; data values in `.data`/`.rodata` are not looked at (that is `compare_ko_v2`); the class REAL is a heuristic and does not prove a bug. Control flow was analysed only for the functions the audit lists. The package audit (all modules built from our sources, 1758 functions) found no difference apart from two layout cases and two functions in another section.

## 4. Do not load these

| module | why |
|---|---|
| `matt` | PARTIAL; all 261 stock functions are written, no stubs, but only 174 of 261 are byte-identical even in the closest build emulation, and the receive engine was written from the disassembly. Two independent reviews and the multiset diff found and removed a use-after-free on every `close()`, a self-deadlock, kCFI traps and a packet-count field read from the wrong place. The remaining unproven functions are `mibbr_main` (middle) and `matt_raw_skb_dump`. See `docs/reviews/matt_review.md` and `matt_review2.md`. |
| `mi_game`, `mi_low_lat_mem` | PARTIAL; only the multiset audit looked at them, no independent code review. `mi_game` had wrong kCFI types (traps) that were fixed; `migt_ioctl` is only partly reconstructed; a few kCFI mismatches in `mi_game` were still open when this was written. |
| `xr_qi`, `xr_qs` | XRING QoS modules (`xr_qi` hooks binder, rwsem and priority inheritance), PARTIAL (`xr_qi` 345 of 371 functions identical), no independent review. |
| `aw882xx_dlkm`, `sia91xx_dlkm`, `tfa98xx_dlkm` | audio amplifier drivers, COMPILES-only: function sets equal the stock, but many bodies, struct layouts and some call arguments are guesses (see their `STATUS.md`). A wrong amplifier driver can misconfigure the speaker amplifier. `sia91xx_tuning_dlkm` is a different, strict-checked module. |
| the whole charging and gauge family | `mca_*`, `mca_business_*`, `mca_strategy_*`, `mca_connector_antiburn`, `mca_wireless_revchg`, `mca_buckchg_jeita`, `mca_pwm_fan`, `bq27z561`, `nuvolta_1652`, `sc8581_sy6541`, `hl7603`, `charger_partition`. Published for the community to read. Two independent reviews of the rebuilt ones found real behaviour differences (below). Several still do not build. Never put one on a real battery without verifying it on hardware first. |

## 5. The reviews of the charging family

Review 1 (`docs/reviews/charging_review.md`) found real differences in four modules that the authors had marked as nearly equal, for example: a prototype error in a call into the fuel-gauge firmware path, a missing UI state-of-charge smoothing, a device-tree property that was never read, a status request with the wrong id, a half-written "charging done" handler, and a template function in the connector anti-burn monitor that did not match the stock at all. It also confirmed 18 modules as equivalent at code and data level (`mca_bmd`, `mca_protocol_pd_class`, `mca_charger_thermal`, `nuvolta_1652`, `mca_business_battery_comp`, `mca_platform_buckchg_class`, `mca_strategy_class`, `mca_event`, `sc8581_sy6541`, `mca_qcom_subpmic_proxy`, `mca_qcom_smem`, `mca_platform_cp_class`, `mca_platform_fg_ic_ops`, `mca_strategy_fg_class`, `mca_vbat_ovp_monitor`, `mca_ibat_ocp_monitor`, `mca_path_control`, `mca_soc_limit`).

Review 2 (`docs/reviews/charging_review2.md`), made after the first round of fixes, found for example that the smart-charge interface enum in the shared stub header had a wrong order (JEITA registered in the wrong slot), and several differences in conditions of the gauge and reverse-charge logic. These were fixed afterwards; no rebuilt charging module is strictly equivalent, and the module `STATUS.md` files say which functions still differ.

Reviews are static reading of disassembly against C. They do not replace running the code.

## 6. Known bugs of the **stock** `matt` module

Found while reviewing the reconstruction. They exist in the stock binary and are reproduced on purpose in our source (identical behaviour was the goal). They concern a module that is disabled by default (`net.matt.enable=0`).

- **S1 (remote)**: `matt_sacktag_write_queue` reads up to 128 SACK blocks (1 KB) from the packet without checking the length of the skb: remote out-of-bounds read.
- **S2 (remote)**: `matt_rcv_data`, when an out-of-order skb overlaps a queued one, frees it and then continues into the acknowledgement part, which reads one byte of the header from the freed data: remote one-byte use-after-free read.
- S3: on unload the IPv4 protocol switch entry is removed with `inet6_unregister_protosw`.
- S4: if the work queue cannot be created, `init_module` returns 0 and the module stays loaded half initialised (out of memory only).
- S5: with `enable=0`, socket creation fails and the destroy path runs on a socket whose timers and queues were never initialised; this is safe only because the kernel zeroes allocations (`init_on_alloc`).
- S6: a store before a NULL check in `matt_transmit_skb` (unreachable, both callers pass a valid pointer).

## 7. Forged CRCs, stub providers

Some modules were only made to compare equal by forging export or import CRCs, by building against stub provider modules, or by `#line` pins. Where and how: `docs/HACKS_AND_FORGED_CRCS.md`. A forged CRC changes only a number in `__versions`/`__kcrctab`; if the real prototype or struct differs from the forged one, the module would still load but the ABI would not match.

## 8. Not covered at all

- Behaviour on hardware. Not one module was loaded.
- Concurrency and races (the reviews looked at lock and reference balance, not at schedules).
- Differences between the 6.6.57 headers the stock modules were built with and the 6.6.144 headers we build with, beyond those that show up as differing bytes (for example `readl()` calls out-of-line `log_read_mmio` in the stock; some public-source modules cannot be made equal for that reason alone).
- Device tree, firmware and userspace parts.
