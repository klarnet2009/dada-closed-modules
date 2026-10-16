EQUIVALENT (reference = OS4 stock `vendor_ramdisk/perfmgr.ko`)

Template: MiCode `warsaw-w-oss` `drivers/mihw/perfmgr/{perfmgr_gpu,perfmgr_ioctl,perfmgr_main,perfmgr_policy,perfmgr_remap}.c` + `perfmgr.h` (copy in `work/re/src/tplm/warsaw/`). The template is the same revision as the stock (HyperOS 3 and OS4 binaries are function-for-function identical): it compiles unchanged against 6.6.144 and all 57 functions match.
Only three ThinLTO/lockdep details had to be reproduced:
- `.str.llvm.<hash>` / `.str.2.llvm.<hash>`: the two literals of `show_gpu_load()` ("gpu load history\n", "\n") are promoted to GLOBAL HIDDEN by ThinLTO (the function is inlined into `gpu_load_show()` of perfmgr_ioctl.c). They are now explicit objects `gpu_load_hdr`/`gpu_load_nl` named `.str.llvm.16574543774624513696` / `.str.2.llvm.16574543774624513696` (the OS4 hash; HyperOS 3 had `...1634248514670012762`) via `a17D_thinlto.h` (`A17D_SYM`).
- `mutex_init(&notify_lock)/mutex_init(&list_lock)` in `init_perfmgr()`: the two lock keys live in one shared `.bss` section (`a17D_lockkeys.h`, same hack as in the other ports).
- local defines `CONFIG_MI_PERFMGR=1 CONFIG_MI_PERFMGR_GPU=1` (the template's Makefile/Kconfig symbols).
Provider: `boost_dcvs_freq(int, u32)` from `qcom-dcvs.ko` -> CRC stub `work/re/src/stubs_a17D/stub_qcom_dcvs.c` (the stub's CRC equals the stock import CRC 0x82c5a5ec in the real Kleaf build; do not flash the stub).

compare_ko vs OS4 stock: 57 functions byte-identical, relocations, objects (incl. the promoted strings), 3 exports + CRCs, 67 imports + CRCs, modinfo (all 60+ `parm`s, `depends=qcom-dcvs`): all OK -> EQUIVALENT (2 compile iterations, 1 bazel build).
