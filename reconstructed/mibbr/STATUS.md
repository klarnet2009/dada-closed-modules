# mibbr - PARTIAL vs the OS4 stock: OS4 is a substantially extended mibbr (11 new sysctls, new debug/trace code) (g4 re-check + g4c 17.10.2026); vs HyperOS 3 it was 26/27

## g4c addendum (17.10.2026)
- Fixed in source (`bbr_bw_to_pacing_rate`): `if (bbr_enable_opt && bbr_pace_factor) rate *= bbr_pace_factor;` (stock: `ldr bbr_enable_opt` first, `cmp x,#0 / ccmp / csinc x9,x9,xzr,ne`). With it `bbr_init_pacing_rate_from_rtt` and `mibbr_cwnd_event` are byte-identical to the OS4 stock (ThinLTO emulation, `work/a17/g4c/q.sh mibbr`).
- The OS4 module is NOT a small delta of the HyperOS 3 one. Static objects of the stock that our source lacks entirely (from its symtab/rodata): sysctls `bbr_srtt_factor`, `bbr_mrtt_factor`, `bbr_min_rtt_win_factor`, `bbr_cap_rtt_factor`, `bbr_avoid_burst`, `bbr_cwnd_limit_factor`, `bbr_probe_rtt_cwnd_factor`,
  `bbr_nonagle`, `bbr_init_cwnd_opt`, `bbr_no_pacing`, `bbr_trace_uid` (+ the existing `bbr_enable_opt`, `bbr_pace_factor`, `bbr_enable_debug`); the sysctl table is 14 entries (896 bytes), `bbr_update_gains.__already_done` (a `WARN_ON_ONCE`/`pr_*_once` in `bbr_update_gains`);
  `mibbr_init` has `if (bbr_enable_opt && bbr_init_cwnd_opt && (srtt==0 || srtt>=8000)) cwnd=44` and `if (bbr_enable_opt && bbr_no_pacing) ...` (a no-pacing branch with a `cmpxchg`/`casal` store at `sk->sk_pacing_status`-like offset, see the tail of stock `mibbr_init`).
  New debug printk formats in the stock (all `bbr_enable_debug`/`bbr_trace_uid` gated): `bbr_limit,sk=%p,target_cwnd=%u,cwnd=%u,ocwnd=%u,flight=%u,bw=%u,una=%u,nxt=%u`, `bbr_tx:sk=%p,appl=%u,ca_state=%u,sp=%u,dp=%u,seq=%u,aseq=%u,len=%u,una=%u,nxt=%u,wseq=%u,cwnd=%u,inflight=%u,srtt=%u,pacing=%lu,wmalloc=%u`,
  `mibbr:sk=%p,bw=%u,pacing_gain=%u,acked_sacked=%u,cwnd_gain=%u,wmalc=%u`, `mibbr:sk=%p,mode=%u,una=%u,nxt=%u,wseq=%u,cwnd=%u,inflight=%u,srtt=%u,mrtt=%u,pacing=%lu`, `mibbr:sk=%p,nonagle=%u,nxt=%u,write_seq=%u`, `mibbr:rtt,sk=%p,rs_rtt=%ld,min_rtt=%u,cwnd=%u,gain=%d`, `mibbr:sk=%p,target_cwnd=%d,cwnd=%d,bw=%u,una=%u,nxt=%u`.
  `bbr_tx` sits in the netfilter output hook (`minet_ext_output_hook6`/`check_and_set_mibbr`, filtered by `is_trace_id()` = the new 24-byte helper using `strncmp` and `bbr_trace_uid`).
- Real work left: re-derive the ~14 new tunables' uses in `bbr_bdp`/`bbr_update_gains`/`bbr_update_min_rtt`/`bbr_set_cwnd`/`mibbr_main` (1270 lines of asm in the stock vs ~950 ours) and the tx-trace hook; est. 150k+ tokens. Not attempted in this round (g4c budget went to binder_gki and the wlan modules).
- Current ThinLTO-emulation state vs OS4: identical `mibbr_cwnd_event`, `bbr_init_pacing_rate_from_rtt`, `mibbr_register`/`update_bbr_uid_list` (1-2 words); differing: `mibbr_main`, `mibbr_init`, `check_and_set_mibbr`, `minet_ext_output_hook6`; missing `is_trace_id`.


Re-checked against `<work-dir>` (real Kleaf build of the unchanged source, `bbr_dir.llvm.<hash>` updated to the OS4 value 8687493924121010519 in `mibbr_internal.h`).
- function set: OS4 has one more function, `is_trace_id` (24 bytes, not recovered); everything else present.
- function bytes differ in 8 of 27: `update_bbr_uid_list` (unchanged: the old `i <= 29` compare), `mibbr_init`, `mibbr_register`, `check_and_set_mibbr`, `minet_ext_output_hook6`, `mibbr_cwnd_event`,
  `bbr_init_pacing_rate_from_rtt`, `mibbr_main`.
- what changed in OS4: (a) the pacing rate: stock multiplies by `bbr_pace_factor` only when `bbr_enable_opt` is set via `cmp/ccmp` on `ldrsw bbr_pace_factor` and `csinc x9,x9,xzr,ne`
  (a 64-bit select, `bbr_enable_opt` loaded in the first block) - ours loads `bbr_enable_opt` late and selects in 32 bit; (b) `mibbr_main` (= `bbr_main`/model update) grew by ~320 instructions: it now tests
  `bbr_enable_debug` right after `bbr_lt_bw_sampling()` and prints a debug record (the 64-bit `bw` value `(delivered<<24)/interval` is kept in x21 across the call), i.e. the OS4 build has debug logging in the model update;
  (c) `is_trace_id` is a new tiny helper used by the netfilter/output hook.
- relocation lists differ in 4 functions (`bbr_config_init`, `mibbr_config_exit`, `init_module`, `cleanup_module`): symbol names of the debug/proc entries moved.
- imports: OS4 imports `strncmp` (not in our build) - consistent with the new `is_trace_id` string compare.
Not re-recovered (needs the new debug printing code and the extra helper); the old source is the best available basis.

## Earlier rounds (HyperOS 3 binary as reference)
`compare_ko.py` (ThinLTO build, see BUILD.bazel/`mibbr_defconfig`): function set (27) OK, relocations OK, global objects (9 incl. `bbr_dir.llvm.18220132966150154302`) OK, exports (none) OK,
imports + CRCs (30) OK, modinfo OK.  Data objects (`mi_tcp_bbr_cong_ops`, `minet_ext_ops`, `bbr_sysctl_table`, `bbr_fops`, `symbol_table`, `bbr_data`, `kprobe`, ...): bytes + relocations identical (`work/scripts/perfB_objcmp.py`).

## What still differs

`update_bbr_uid_list` (1 function, 2 instructions): the loop-continue test after `strsep()` is `cmp w19,#0x1d ; b.ls` in stock (`i <= 29`) but `cmp w19,#0x1e ; b.lo` (`i < 30`) in our build.
Everything else in the function is identical (unsigned `i`, `uxtw` index, order token-NULL check then `i` check). Tried (in `work/re/src/perfB_lab3`, 13 loop shapes: while/for/do-while,
`i < 30`, `i <= 29`, `if (i >= 30) break` before/after the NULL test, ...): LLVM canonicalises all of them to `ult 30`; only the shape with the `i` test BEFORE the NULL test keeps a `ugt 29`
(that one has the wrong test order). `-mllvm -aarch64-enable-condopt/ccmp/cond-br-tune=false` change nothing. Unknown original spelling.

## Source basis

Congestion-control core = Linux `net/ipv4/tcp_bbr.c` (BBRv1, 6.6) with Xiaomi changes, three translation units as in the stock ThinLTO build
(`tcp_mibbr.c`: BBR + sysctl + `xm_set_mibbr`/`mibbr_register`; `mibbr_config.c`: uid white list in `/proc/mibbr/config`; `minet_ext.c`: nf hooks, kprobe `kallsyms_lookup_name` trick, init/exit).
Xiaomi changes recovered from the binary:
- sysctl `net/bbr/{bbr_enable_opt=1,bbr_enable_debug,bbr_rtt_factor=10,bbr_pace_factor=10}`; with `bbr_enable_opt` the pacing rate is multiplied by `bbr_pace_factor` (in `bbr_bw_to_pacing_rate`, not in
  `bbr_rate_bytes_per_sec`) and `bbr_bdp()` uses `srtt_us>>3 * bbr_rtt_factor / 10` instead of `min_rtt_us`.
- `bbr_init()` ends with `if (srtt_us == 0 || srtt_us >= 8000) tcp_snd_cwnd_set(tp, 44)` (spelling matters: InstCombine turns `eq0 || ge` into one range compare, `!(x && x < 8000)` gives a different form).
- the module does not call `tcp_register_congestion_control()`: `xm_set_mibbr()` is a `tcp_reinit_congestion_control()` copy, `tcp_cleanup_congestion_control`, `tcp_init_congestion_control`,
  `minmax_running_max`, `register_btf_kfunc_id_set` are looked up with `kallsyms_lookup_name` (found through a kprobe on it) and called through function pointers (kCFI checked).
- netfilter LOCAL_OUT hooks (prio 1) switch sockets of white-listed uids (`bbr_uid_list[30]`, written through `/proc/mibbr/config`, "clear" empties it) to `mibbr` when ca name != "mibbr".
- printk()s have no level prefix.

## Lessons

- ThinLTO matters: `defconfig = "mibbr_defconfig"` (CONFIG_LTO_CLANG_THIN) made `is_mibbr_white_app`, `bbr_file_write`, `init_module` identical without any local-bounds/sanitizer hack.
- Separate translation units are needed for callee return values to stay opaque (otherwise IPSCCP folds `mibbr_register()`/`bbr_config_init()` results in `init_module`).
- `bbr_dir.llvm.<hash>` (GLOBAL HIDDEN) is reproduced with `__asm__("bbr_dir.llvm.18220132966150154302") __attribute__((visibility("hidden")))`.
- 6.6.57 vs 6.6.144 header drift: `__access_ok()` (see debug_ext) and `INET_ECN_dontxmit`.
