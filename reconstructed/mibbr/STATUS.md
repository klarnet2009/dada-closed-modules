# mibbr - 26 of 27 functions byte-identical

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
