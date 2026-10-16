# Method: reverse-engineering and rebuilding the closed modules

> Working notes written as instructions for the AI agents that did most of the work; kept as-is because they document the exact recipe and the lessons learned. Host names, user names and local paths were replaced by placeholders (`<kernel-tree>`, `<work-dir>`, `<build-server>`).

# Brief: reverse-engineer closed Xiaomi/Qualcomm kernel modules back to compilable C (Xiaomi 15 "dada", kernel 6.6, arm64)

Goal: for each assigned stock module (`.ko` from the phone) write C source that compiles with the SAME toolchain and
produces a module that `compare_ko.py` reports as **EQUIVALENT** (every function byte-identical, relocations identical with
strings/targets resolved, same exports+CRC, same imports+CRC, same modinfo). Hardware is NOT available: equivalence is
checked statically. Modules are GPL, so reconstructing them is legitimate; output is GPL-2.0.

## Where things are (Windows host, Git Bash available; project root `<workdir>/`)
- Stock modules: `work/re/missing26/<mod>.ko`. Per-module dumps: `work/re/dump/<mod>/{dis.txt,syms.txt,rodata.txt,modinfo.txt,sections.txt}`
  (`dis.txt` = `llvm-objdump -dr`, relocations inline; `rodata.txt` = all strings with offsets).
- BTF hints (parameter names, local struct members, function names; base types print as `T<id>`, unresolved):
  `python -I work/scripts/btf_dump.py local work/re/missing26/<mod>.ko`
- Xiaomi reference code (different SoC "XRING", same vendor codebase; headers/types/prototypes for mca_*, mi_net, touch...):
  `work/re/xiaomi-modules/`. Other Xiaomi code: blobless git clones in `work/srcsurvey/micode_{dada,bixi,xuanyuan,miro,onyx}`
  and `work/srcsurvey/op13_{kernel,modules,common}` (OnePlus 13, Qualcomm vendor drivers: wlan, audio, camera, display...).
  Read any file lazily with `git -C work/srcsurvey/<clone> show HEAD:<path>`. Other MiCode branches (older Xiaomi kernels with
  hwid, perfmgr, mi_memory, xm_power... ) are fetchable on the server: bare repo `<build-dir>`
  (`git -C ... fetch --depth=1 --filter=blob:none o refs/heads/<branch>:refs/remotes/o/<branch>`, then `git show`).
  Kernel headers: server `<build-dir>` (+ `arch/arm64/include`).
- Build server (all compiling happens there; NEVER run WSL/heavy builds on this PC):
  `ssh -i "<workdir>/keys/<ssh-key>" -o BatchMode=yes <user>@<build-server> '<cmd>'`
  Workspace `<build-dir>` (Kleaf/Bazel, DDK). Your packages live in `<build-dir>`.

## Workflow (scripts are in `work/scripts/`, already deployed on the server)
1. Source lives locally in `work/re/src/<module>/` : `<module>.c` (+ headers) and `BUILD.bazel` (see `work/re/src/millet_oem_cgroup/BUILD.bazel`).
2. `bash work/scripts/re_push.sh <module>`  uploads the dir, builds `//re/<module>:<module>` and runs the comparison. (~15-60 s)
3. `ssh ... '<build-dir> <module> 4'`  prints a normalized disassembly diff stock(<) vs rebuilt(>) - this is the
   main tool: iterate on the C until the diff is empty. `compare_ko.py ... -v` shows relocation diffs.
4. Imports needing a provider (symbols exported by other Xiaomi/Qualcomm modules, not by the GKI kernel) need a STUB module
   exporting them with the right prototype, because modpost refuses undefined symbols. Put stubs in YOUR OWN package
   `re/stubs_<yourtag>/` (see `work/re/src/stubs/` for `mca_log`/`smem` stubs, which you may depend on read-only).
   The prototype is right when the stub's export CRC equals the stock module's `__versions` CRC (compare_ko prints
   imports mismatches) - this is how types/qualifiers of internal APIs are confirmed.
5. Status per module: write `work/re/src/<module>/STATUS.md` (EQUIVALENT / which functions still differ and why).

## Hard-won facts (all verified on the first two modules)
- Build flags must stay `--lto=none --config=stamp` (scripts do it); otherwise bazel rebuilds the kernel (slow + disk). Use `deps = ["//common:all_headers_aarch64"]`.
- Stock modules were compiled with LTO-style READ_ONCE (ldar/ldapr): put `local_defines = ["CONFIG_LTO=1"]`. Vendor CONFIG_* that GKI
  lacks (e.g. `CONFIG_QCOM_SMEM=1`) must be passed as `local_defines` too; check headers' `#if IS_ENABLED(...)` when a prototype looks wrong.
- `kcfi` hash words (`.word 0x...` before each function) encode the exact function type: a mismatch means wrong arg/return types
  (e.g. `bool *` vs `u8 *`; also strict-aliasing effects: bool* stores do not alias u8 loads -> different reload pattern).
- `__LINE__` values are baked into log calls (`mov w2,#NN`): reproduce them by placing statements on exact source lines
  (see `work/re/poc/gen_mca_qcom_smem.py` for a generator that pins line numbers).
- Format-string literals are in rodata.txt (kernel log prefixes like `\x013` = KERN_ERR "3"); `__func__` strings appear too.
- Struct layouts: derive from load/store offsets; BTF member names help; sizes of global objects must match `syms.txt`.
- Function order and `static`/global linkage must match symtab (`syms.txt`); local helper functions can be inlined by the compiler, so
  an `inline`/`noinline` choice may be needed to match. Exports order == EXPORT_SYMBOL statement order.
- The comparison is per function (bytes + relocations); layout of sections differs from stock (stock was linked per-function-section), that is fine.

## Rules
- Do not touch anything on the server outside `<build-dir> modules or stubs_<tag>>` and `<build-dir>`. Other agents work in parallel
  in other packages; do not edit `re/stubs`, shared scripts, or others' modules. Bazel serializes commands (lock), that is normal.
- Server disk is nearly full (<10 GB). `re_build.sh` aborts under 4 GB. Never clone big trees on the server; no `bazel clean`, no deleting other data.
- No outward-facing actions: no pushing to remote repos, no emails, no phone access, nothing flashed.
- Do not stop at "close enough": iterate until EQUIVALENT, or document precisely why not (e.g. needs structure you cannot recover) in STATUS.md.
  If a module is stuck after ~40 build iterations, write what you learned and move to the next module.
- Final answer: per module one line (EQUIVALENT / PARTIAL with the remaining diff) + general lessons that help other modules.

## Lessons added during the run (from agent re-audio, verified on sia91xx_tuning -> EQUIVALENT)
- If function bytes differ only in `bl` to a static function of the same module: add `copts = ["-ffunction-sections", "-fdata-sections"]` to the
  `ddk_module` in BUILD.bazel. The stock .ko keeps intra-module calls as `R_AARCH64_CALL26 .text+off` relocations (per-section LTO-style link);
  without function sections the assembler resolves them and the bytes differ.
- Every `mutex_init()` / `init_waitqueue_head()` / `spin_lock_init()` static `__key` is a zero-size object that the stock link merges into ONE .bss address,
  so compare_ko resolves them all to `<func>.__key`; a non-LTO link keeps them apart (init relocs differ by symbol name only). Fix: redefine the init macro locally
  so each key is `static struct lock_class_key __key __attribute__((section(".bss.somename")))` (one shared section => identical codegen).
  Working example: `work/re/src/sia91xx_tuning/sia91xx_tuning.c` (SIA_KEY_SECTION).

## Tools/lessons from agent re-misc (verified: qcom_adsp_pd_protocol EQUIVALENT incl. its exported ops CRC)
- Stock modules WITH BTF and ground-truth export CRCs are in `work/dada-devel/device_xiaomi_dada-kernel/vendor_dlkm/*.ko` (e.g. `mca_protocol_pd_class.ko`):
  `python -I work/scripts/btf_dump.py local <that .ko>` prints full struct/ops definitions with member names; `from crctool import exported` gives the stock CRC of every export,
  so a stub/header prototype can be verified per function.
- genksyms CRC emulator on the server: `<build-dir> <file.c> -I<hdrdir>` prints `#SYMVER name 0xcrc`. It matches the stock CRC exactly only when the function NAME and the
  type SPELLING match (`unsigned int` vs `u32` vs `uint32_t`, enum enumerators with/without `= N`, hex vs decimal, `unsigned long` vs `u64` all change the CRC): brute-force spellings one function at a time.
- `#line N` before a log statement pins `__LINE__`. `__attribute__((no_sanitize("bounds")))` removes the local-bounds `brk #1` trap that the stock LTO link folded away.

## KEY: stock modules are ThinLTO builds (agents re-audio + mcaA, verified: tas25xx 117 functions identical; mca_adsp_glink identical except __LINE__)
- Do NOT use `copts = ["-flto=thin"]`. In the module's BUILD.bazel add `defconfig = "<mod>_defconfig"` to `ddk_module`, the file containing exactly two lines:
  `# CONFIG_LTO_NONE is not set` and `CONFIG_LTO_CLANG_THIN=y`. DDK merges it into the module's .config, so the kernel's own CC_FLAGS_LTO (-flto=thin -fsplit-lto-unit) and
  KBUILD_LDFLAGS (-mllvm -import-instr-limit=5) apply; cross-file inlining and which statics get promoted then match stock. It does NOT rebuild the kernel (first build ~9 min, later ~15 s).
  Still needed with it: (a) the lock-key section hack for mutex_init/init_waitqueue_head (module.lds merging is absent); (b) ThinLTO-promoted statics appear in stock as GLOBAL HIDDEN
  `name.llvm.<hash>`; the hash cannot be reproduced, so define such objects as non-static `T name __asm__("name.llvm.<hash>") __attribute__((visibility("hidden")))`
  (example `work/re/src/tas25xx/src/tas25xx-misc.c`); (c) for multi-line macro calls `__LINE__` is the line of the closing paren.
- Instant Bazel-free compile loop on the server (0.5 s, no lock, no kernel rebuild): `<build-dir>` is the extracted modules_prepare outdir; scripts
  `<build-dir> <pkgdir-under-<build-dir> <abs out.o> [extra clang flags]` (exact kernel flags; add -flto=thin) and
  `<build-dir> <pkgdir> <stockmod> [flags]` (LTO emulation + per-function diff vs stock, __LINE__ immediates masked; `fdiff_mcaA.py`). They hardcode
  `-I<build-dir>` and the module's first .c: COPY and edit for your own paths. IR dumps: `-S -emit-llvm`, `-mllvm -print-after=dse -mllvm -filter-print-funcs=<fn>`.
- Per-function stock-vs-built disassembly diff with resolved reloc targets: `python3 <build-dir> <mod> [func...]`. genksyms: `<build-dir>`
  plus `genksyms -D` dump: `zlib.crc32` of the dumped stream equals the SYMVER (fast offline brute force of type/field spellings).
- NEVER pass extra flags to the Kleaf bazel build (`--debug_make_verbosity`, `--subcommands`...): it invalidates the kernel build and blocks every agent for 10-20 minutes.

## Lessons from agent re-audio (tas25xx: 117 functions identical; verified)
- BEST way to get the stock's ThinLTO behaviour (cross-file inlining, which statics get promoted): in the module's BUILD.bazel add `defconfig = "<mod>_defconfig"` to `ddk_module`; the file holds
  `# CONFIG_LTO_NONE is not set` and `CONFIG_LTO_CLANG_THIN=y`. The module then builds with the kernel's own LTO flags (`-flto=thin -fsplit-lto-unit`, link `-mllvm -import-instr-limit=5`) without
  rebuilding the kernel (first build ~9 min, later ~15 s). `copts = -flto=thin` alone imports far more than the stock. Working example: `work/re/src/tas25xx/`.
- ThinLTO promotes file-local objects used by cross-TU inlined code to GLOBAL HIDDEN `name.llvm.<hash>`; the hash is the bitcode hash and cannot be reproduced. compare_ko compares these names, so define such objects
  as non-static: `T name __asm__("name.llvm.<hash>") __attribute__((visibility("hidden")))` (see tas25xx-misc.c). The stock symtab shows which ones are promoted (`grep llvm syms.txt`).
- In a multi-line macro call (dev_err(...) over several lines) `__LINE__` is the line of the closing parenthesis. A source line can be pinned with `#line N` or padding comments.
- Writing code through the bash tool: backslashes in heredocs are halved (a `\n` inside a python heredoc string became a real newline); create/edit C files with Write/Edit.
- Export CRC debugging: `genksyms -D` dumps the exact stream; `zlib.crc32(stream)` equals the SYMVER, so field/type spellings can be brute forced at ~2M candidates/s in Python
  (see <build-dir> audio_fdiff.py on the server; scripts in the agent scratchpad). The return-type spelling (`int` vs `int32_t`, header prototype visible or not) changes the prefix.
