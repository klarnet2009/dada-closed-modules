# Status and limits: what is proven here and what is not

State of 18.10.2026 (evening). Everything below is static verification of binaries on a build machine. **No module in this repository has been loaded or run on a phone. Nothing was flashed. Nothing was tested on a device.**

## 1. Modules per status

145 modules are tracked (`reconstructed/`, `ported/` and the notes in `docs/blocked/`):

| status | modules | meaning |
|---|---|---|
| EQUIVALENT | 100 | every function, relocation, object, export and import (with CRC) and the modinfo match the stock module |
| PARTIAL | 40 | interfaces and logic as far as checked, but some functions differ (layout and register allocation in most, real code differences in some: see the `STATUS.md` of the module) |
| COMPILES-only | 4 | builds, function set equals (or nearly equals) the stock one, but many bodies differ |
| BLOCKED | 1 | does not compile; only an inventory exists: `bq27z561` |

(3 modules appear twice, once under their HyperOS 3 name and once under their HyperOS 4 name: `tas25xx`, `typec_analog_acc`, `sia91xx_tuning`.)

Of the 100 EQUIVALENT modules, 99 passed the strict check described below; the rest (1) was compared only with the first-generation tool. The per-module value is in the last column of the tables in the README. **No module of the charging family that was rewritten or reviewed (`mca_buckchg_jeita`, `mca_strategy_fg_comp`, `mca_connector_antiburn`, `mca_wireless_revchg`, `mca_business_charger_comp`, `mca_pwm_fan`, `mca_smart_charge`, `mca_quick_wireless`, `mca_basic_wireless`, `mca_strategy_buckchg`, `mca_strategy_quickchg`) is STRICT-EQUIVALENT**: they are PARTIAL or COMPILES-only, and their build metadata (no `intree=Y` flag) and `depends=` (forged `os4_*` providers) never match the stock.

A module is only called EQUIVALENT when the check of the day found nothing. Several times in this project the check was found to have holes and modules had to be downgraded or fixed; see section 2 and `docs/reviews/`.

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

Even the strict tool misses things. A later independent review of the Wi-Fi drivers (section 6) found that 14 of 21 objects, which had been filed as "relocation layout differences" for several rounds, really differed in content (a netfilter hook registered at the wrong place, wrong policy tables, a wrong default). And a string-text scan found one log message that differed while all function bytes were equal. What the strict tool still does not check: `.eh_frame` and `.BTF`; memory accesses without relocations; `__FILE__` in the bug table is compared by basename; trailing zeros of anonymous pieces are ignored. Metadata always differs and is accepted: `intree=Y`, `scmversion=`, `srcversion=`, `staging=Y` versus `built_with=DDK`, the LTO `.note.Linux`, and `depends=` where a build named stub providers.

## 3. What a multiset diff proves, and what it does not

For modules that are not byte-identical (every PARTIAL module and the charging reviews) we compared, per function, the **multiset of instructions with registers masked**: stack accesses, frame setup, register moves, unconditional jumps and `ret` are dropped; `ldp`/`stp` are split; comparisons are normalised. Functions of the same module that were inlined in one build and not in the other are expanded before comparing. The result per function is one of LAYOUT (multisets equal), INLINE (equal after expansion), SELECT (only ALU/branch instructions differ), CONST (immediates differ too) and REAL (loads, stores, calls or traps differ). See `docs/reviews/multiset_audit.md` (the package and 31 PARTIAL modules) and `docs/reviews/multiset_audit2.md` (second part, with the bugs it found); both are English translations of the original working reports.

It proves, when the multisets are equal: the function performs the same memory accesses (opcode, width, offset), calls the same functions the same number of times, contains the same constants and the same trap instructions (`brk`, from `BUG_ON`, fortify, bounds checks). That is what found a self-deadlock (`getsockopt` took a lock twice), wrong kCFI prototypes that would trap on the first call, a missing SACK loop that the sanitizer had replaced by `brk`, and several fields read from the wrong source (for example `p->on_rq` instead of `p->se.on_rq` in `mi_game`, an unzeroed array in `mi_low_lat_mem`).

It does **not** prove equivalence: operations may be permuted; an inverted branch with a mirrored layout has the same multiset; data values in `.data`/`.rodata` are not looked at (that is `compare_ko_v2`); the class REAL is a heuristic and does not prove a bug. Control flow was analysed only for the functions the audit lists. The package audit (all modules built from our sources, 1758 functions) found no difference apart from two layout cases and two functions in another section.

## 4. Do not load these

| module | why |
|---|---|
| `matt` | PARTIAL; all 261 stock functions are written, no stubs, but only 174 of 261 are byte-identical even in the closest build emulation (87 of 261 in the real Kleaf build with LTO off), and the receive engine was written from the disassembly. Two independent Opus-level reviews and the multiset diff found and removed a use-after-free on every `close()` (extra `sock_put`), a self-deadlock, kCFI traps, a jhash argument order, and a packet-count field read from the wrong place (`shinfo->gso_segs` instead of `cb->gso_segs`, 11 places). The second review found no new reconstruction error and allows loading on a throw-away test phone with `net.matt.enable=0` only; the middle of `mibbr_main` and the second half of `matt_raw_skb_dump` are still not proven line by line. The **stock** module has remote bugs (S1/S2 below). See `docs/reviews/matt_review.md` and `matt_review2.md`. |
| `mi_game`, `mi_low_lat_mem` | PARTIAL (75 of 239 and 25 of 111 functions byte-identical in the real build); no independent code review. Several rounds fixed wrong kCFI prototypes (traps), a wrong field (`on_rq`), a wrong `glk` value in `xr_set_next_freq`, a missing zeroing, wrong export kinds, a swapped trace-field pair. Two more kCFI hashes were found by the substitution-aware search. Many differences remain that were only read, not proven. `mi_damon` (PARTIAL, small) belongs to the same family of fixes. |
| `xr_qi`, `xr_qs` | XRING QoS modules (`xr_qi` hooks binder, rwsem and priority inheritance), PARTIAL: `xr_qi` 351 of 371 functions identical (10 export CRCs forged by GENKSYMS pads), `xr_qs` 114 of 120. Behaviour bugs were found by the multiset diff (an unbounded cmpxchg retry loop where the stock retries 3 times, a missing trace call). Community-only: never flash; no independent review. `docs/reviews/stub_inventory.md` lists the stub functions that were replaced. |
| `aw882xx_dlkm`, `sia91xx_dlkm`, `tfa98xx_dlkm` | audio amplifier drivers, COMPILES-only (function bytes identical: 153 of 267, 48 of 104, 150 of 389): function sets equal the stock, but many bodies, struct layouts and some call arguments are guesses (see their `STATUS.md`). For example the chip-type enum of `sia91xx_dlkm` was wrong until a late round, and the chip `0x2116` branch of `aw882xx_dlkm` was missing. A wrong amplifier driver can misconfigure the speaker amplifier. `sia91xx_tuning_dlkm` is a different, strict-checked module. |
| `mitee_dlkm` | PARTIAL (149 of 182 functions identical, not strict-equivalent). The only own-source module of a TEE (OP-TEE based) transport: the Gunyah message-queue transport (`mitee_msgq.c`) has no public source and was recovered from machine code, the wire format is **inferred**, field names are guessed. It builds only with forged provider stubs (`os4_*`, not published, never to be shipped). Do not load: a wrong TEE transport is a security risk. |
| the whole charging and gauge family | `mca_*`, `mca_business_*`, `mca_strategy_*`, `mca_smart_charge`, `mca_quick_wireless`, `mca_basic_wireless`, `mca_connector_antiburn`, `mca_wireless_revchg`, `mca_buckchg_jeita`, `mca_pwm_fan`, `bq27z561`, `nuvolta_1652`, `sc8581_sy6541`, `hl7603`, `charger_partition`. **Published for the community to read and review only. Never flash, never put into a ROM, never put on a real battery.** See section 5. |
| `qca_cld3_*` Wi-Fi drivers | PARTIAL, five variants of one shared source; 97-99% of the functions are byte-identical (the higher figure with a line-number pin table) but 117-421 functions per variant still differ in the strict comparison, three review rounds found real behaviour differences that were fixed, and nothing has run on hardware. Treat as research code (section 6). |

## 5. The charging family

The family is **community-only and is never to be flashed**: a wrong charging or fuel-gauge driver can damage a battery. The kernel package leaves all of it stock. None of the rebuilt ones is STRICT-EQUIVALENT, and none was run on a device. Per module (functions byte-identical to the OS4 stock in the real build):

| module | status | identical |
|---|---|---|
| `mca_buckchg_jeita` | PARTIAL | 13 of 17 |
| `mca_strategy_fg_comp` | PARTIAL | 79 of 97 |
| `mca_connector_antiburn` | PARTIAL | 12 of 17 |
| `mca_wireless_revchg` | PARTIAL | 43 of 45 |
| `mca_business_charger_comp` | PARTIAL | 41 of 45 |
| `mca_pwm_fan` | PARTIAL | 16 of 20 |
| `mca_smart_charge` | PARTIAL (was BLOCKED) | 29 of 35; one export CRC forged by a GENKSYMS pad |
| `mca_basic_wireless` | PARTIAL (was BLOCKED) | 60 of 73; the body of the interrupt handler inside `process_irq` is still template code, `monitor_work` and `loop_work` differ |
| `mca_strategy_buckchg` | PARTIAL (was BLOCKED) | 41 of 61; the main state machine and several workers are still XRING template code, not checked against the stock |
| `mca_quick_wireless` | PARTIAL (was BLOCKED) | 23 of 37; hardware-taper, bridge-switching and several other paths are not implemented |
| `mca_strategy_quickchg` | COMPILES-only (was BLOCKED) | 4 of 83 (57 present); behaviour is the older XRING template, 26 stock functions are missing |
| `bq27z561` | BLOCKED | inventory only, no compile attempt |

Three independent reviews of the rebuilt modules (`docs/reviews/charging_review.md`, `charging_review2.md`, `charging_review3.md`):

- Review 1 found real differences in four modules that the authors had marked as nearly equal, for example: a prototype error in a call into the fuel-gauge firmware path, a missing UI state-of-charge smoothing, a device-tree property that was never read, a status request with the wrong id, a half-written "charging done" handler, and a template function in the connector anti-burn monitor that did not match the stock at all. It also confirmed 18 modules as equivalent at code and data level (`mca_bmd`, `mca_protocol_pd_class`, `mca_charger_thermal`, `nuvolta_1652`, `mca_business_battery_comp`, `mca_platform_buckchg_class`, `mca_strategy_class`, `mca_event`, `sc8581_sy6541`, `mca_qcom_subpmic_proxy`, `mca_qcom_smem`, `mca_platform_cp_class`, `mca_platform_fg_ic_ops`, `mca_strategy_fg_class`, `mca_vbat_ovp_monitor`, `mca_ibat_ocp_monitor`, `mca_path_control`, `mca_soc_limit`).
- Review 2, made after the first round of fixes, found for example that the smart-charge interface enum in the shared stub header had a wrong order (JEITA registered in the wrong slot), and several differences in conditions of the gauge and reverse-charge logic.
- Review 3 (the six modules fixed after reviews 1 and 2) found nothing above LOW for battery safety: a cosmetic array-bound constant, a race-only re-read of a JEITA index, and five counters loaded with another signedness; no extra or missing writes to charger, fuel gauge, adapter, GPIO or voters in any of the 37 differing functions. All earlier findings were re-verified as closed. It does not make the family safe: no hardware test exists, control flow was compared symbolically for the differing functions only, and the newer modules (`mca_smart_charge` ... `mca_strategy_quickchg`) were **not** part of it.

**Enum-name audit** (`docs/reviews/charging_enum_audit.md`): the shared stub headers carried enumerator names and values that differ from the stock BTF (178 mismatching enumerators over all stub trees, mostly `charge_dfx_ele` and `mca_event_ele`). The BTF enums of all 104 stock charging modules were dumped and compared with every stub tree and every finished charging module. Result: no finished module passes a wrong number (the immediates in the `.ko` equal the stock ones); wrong *names* (right value, name of another enumerator, i.e. a misleading source) were found in `mca_business_charger_comp` and `mca_strategy_fg_comp` and fixed, with an identical rebuilt binary; a few harmless alias names remain in other modules and are listed in the report. The old shared stub headers carry a warning comment.

Reviews are static reading of disassembly against C. They do not replace running the code.

## 6. Wi-Fi (`qca_cld3_*`)

See `docs/WIFI.md`. Short version: five variants (`peach`, `peach_v2`, `kiwi_v2`, `qca6750`, `wcn7750`) of the public Qualcomm/OnePlus `qcacld-3.0` tree plus our Xiaomi extension code; all five PARTIAL; no function of any variant is missing or extra; 17825 of 18446 functions of `peach_v2` are byte-identical (18313 with a `#line` pin table that only equalises line numbers). An independent review (`docs/reviews/wifi_review1.md`) found 11 findings (F1-F11); F1-F7 were fixed in the next round (F1: the P2P header-compression netfilter hook was registered at the wrong hook point, which would have dropped every compressed packet of a Xiaomi peer). After that round the remaining differences were examined function by function and no further functional difference was found at the level of binary comparison. That is a statement about what binary checking can see, not a statement that the driver works.

## 7. Known bugs of the **stock** modules

Found while reviewing the reconstructions. They exist in the stock binaries and are reproduced on purpose in our sources (identical behaviour was the goal). Please do not copy them into anything that handles untrusted input.

`matt` (disabled by default, `net.matt.enable=0`):

- **S1 (remote)**: `matt_sacktag_write_queue` reads up to 128 SACK blocks (1 KB) from the packet without checking the length of the skb: remote out-of-bounds read.
- **S2 (remote)**: `matt_rcv_data`, when an out-of-order skb overlaps a queued one, frees it and then continues into the acknowledgement part, which reads one byte of the header from the freed data: remote one-byte use-after-free read.
- S3: on unload the IPv4 protocol switch entry is removed with `inet6_unregister_protosw`.
- S4: if the work queue cannot be created, `init_module` returns 0 and the module stays loaded half initialised (out of memory only).
- S5: with `enable=0`, socket creation fails and the destroy path runs on a socket whose timers and queues were never initialised; this is safe only because the kernel zeroes allocations (`init_on_alloc`; check `/proc/cmdline` has no `init_on_alloc=0`).
- S6: a store before a NULL check in `matt_transmit_skb` (unreachable, both callers pass a valid pointer).

`qca_cld3_*` (found by `docs/reviews/wifi_review1.md`):

- the P2P header-compression code (`dlhc_remember`, `__rx_inet_hook`) accepts remote input from any P2P peer without proper length checks (out-of-bounds read, a possible BUG on a non-linear skb) and ignores its enable sysctl;
- the Xiaomi fast-ACS path in `wlan_hdd_cfg80211_do_acs` fills fixed 101-entry arrays from a user-supplied list: an out-of-range count traps (kernel panic from a vendor command that needs CAP_NET_ADMIN) or overruns a heap buffer.

Because the F1 fix restores the stock hook point, these remote parsers are reachable again exactly as in the stock driver.

Also: the exported charging-family sources carry some stock quirks on purpose, for example a checksum in `mca_smart_charge` that sums `total_len` bytes without a bound check, and a copy without NUL in `mca_basic_wireless` debug code.

## 8. Forged CRCs, stub providers

Some modules were only made to compare equal by forging export or import CRCs, by building against stub provider modules, or by `#line` pins. Where and how: `docs/HACKS_AND_FORGED_CRCS.md`. A forged CRC changes only a number in `__versions`/`__kcrctab`; if the real prototype or struct differs from the forged one, the module would still load but the ABI would not match.

## 9. Kernel drift: 6.6.57 versus 6.6.144

The stock modules were built against kernel 6.6.57 headers, ours are built against 6.6.144 (the kernel of the package). Some differences in function bytes, bug-table line numbers and export CRCs are therefore not errors in our source: `readl()`/`writel()` call `log_*_mmio` unconditionally in 6.6.57, `netif_tx_stop_queue()` writes `trans_start` and a barrier in 6.6.144, `poll_wait()` has an extra barrier, `task_struct` members moved (`mm` at +0x5a8 versus +0x5a0), `cfg80211.h`/`skbuff.h`/`netlink.h` line numbers moved (151 of the 155 Wi-Fi functions with differing bug-table lines are header vintage). The Wi-Fi build therefore force-includes `ported/qca_cld3_xiaomi_ext/xm_compat_6657.h`, which restores the 6.6.57 bodies of `netif_tx_stop_queue`, `readl` and `writel`. **That header is a build-environment emulation, not Xiaomi code.** It also disables a macro-redefinition warning for every translation unit. Several forged CRCs have the same cause (a struct of the other tree).

## 10. Not covered at all

- Behaviour on hardware. Not one module was loaded.
- Concurrency and races (the reviews looked at lock and reference balance, not at schedules).
- Differences between the headers and structs of the two kernels beyond those that show up as differing bytes.
- Device tree, firmware and userspace parts.
