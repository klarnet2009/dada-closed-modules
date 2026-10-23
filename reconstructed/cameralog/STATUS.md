# cameralog - PARTIAL (10 of 11 functions byte-identical vs the OS4 stock, re-checked by g4 17.10.2026)

## G17a (19.10.2026) - global OS4.0.0.7 re-measurement and function classification (first line unchanged unless noted: still PARTIAL, no hardware test)
Global OS4.0.0.7 stock (<work-dir>) vs CN OS4.0.0.6 stock: code-identical (cn_vs_gl.csv, compare_ko_v2 stock-vs-stock: STRICT-EQUIVALENT). Real Kleaf build (scratch <work-dir> tag g17a, bazel-out removed) vs the GLOBAL stock: function set 11 OK; 10 of 11 byte-identical; cameralog_ioctl = CODEGEN (return-value phi and block layout: 41/41 facts, only the ret phi form differs); anon-data = jump table section name. 0 BEHAVIOUR differences, no source change.
Tools: <work-dir> (kb17.sh = Kleaf build + compare_ko_v2 + preamble_cmp, sym17.sh = symcmp4 on every differing function, str17.py = printk format scan, fd17.sh = disassembly diff), results in res/<module>.cmp / .sym. Hacks: none added; earlier hacks (forged CRC / stubs / codegen hints) unchanged and still listed above.


g4j (17.10.2026): real Kleaf build re-run: all structural checks OK, only cameralog_ioctl differs (block layout). New try: `__builtin_expect_with_probability(debugLevel > 0, 1, p)` on the toggle test, p = 0.05..0.95 (7 values): 62 -> 60 word diff at best (p <= 0.35), not the fix; source unchanged. Stock lays out the toggle ON block (`mov w9,#1; str`) and OFF block (`str wzr`) after the SET_DEBUG block and shares one `mov x0,xzr; b epilogue` tail; ours tail-duplicates `mov x0,xzr` into the toggle OFF block.

g4d (17.10.2026): real Kleaf build re-run against the OS4 stock: function set (11), relocations, objects, export CRC, imports (27) + CRCs, modinfo OK; only `cameralog_ioctl` differs. No change made (the block-layout problem is unchanged).


Re-checked against `<work-dir>` with a real Kleaf ThinLTO build of the unchanged source: the OS4 binary has the same function set, relocations, objects, export CRC, imports (27) and modinfo;
only `cameralog_ioctl` differs (block layout, see below). No source change was needed for OS4.

## Earlier rounds
Real Kleaf build with `defconfig = cameralog_defconfig` (CONFIG_LTO_CLANG_THIN=y): function set OK, relocations of all functions identical, global objects,
export CRC, imports CRCs (27) and modinfo identical. Only cameralog_ioctl differs.
(With the default LTO_NONE build also cameralog_write and the out-of-line _copy_to_user/_copy_from_user differed; ThinLTO fixed them, and the
no_sanitize("bounds") workaround is no longer needed.)

cameralog_ioctl: same logic, same instructions, but a different block layout: stock lays out the toggle/ON/OFF store blocks and the success block
(`mov x0,xzr`) before the FIFO_LEN case and puts the epilogue after the -EFAULT block; ours puts `mov x0,xzr` immediately before the epilogue and
turns the toggle into a different branch shape. ~100 source spellings tried (order of cases, break/return/ret-variable styles, goto-shared ON/OFF blocks,
ternary, volatile debugLevel): the compiler normalizes them to the same output. The 0.5 s ThinLTO emulator (<build-dir>) gives the same result.

How the rest was matched: struct camlog_message{u32 uid,pid; char msg[1024]} (0x408), `struct kfifo` byte fifo of 6*0x408, debug prints
`if (debugLevel > 0) printk(KERN_DEBUG ...)`, ioctl 0x40044d01..07 / 0x80044d04 / 0x80084d05, open-coded poll_wait without smp_mb (stock headers have none),
`int rc = copy_to_user()/copy_from_user()` in read/write, `__init_waitqueue_head` key == &camlog_is_not_empty via asm adrp/add, mutex key in own section.

Second round (this session): current source is the goto-label variant (toggle `if (debugLevel > 0) goto debug_off; goto debug_on;`, shared `ok:`/`fault:` tails, `default: return -ENOTTY`);
the emulator diff is 62 words instead of 138. LLVM IR of our version shows the toggle edges reach the same ON/OFF blocks as stock, so the remaining difference is
machine-level (BranchFolding tail merging / block placement of the success block), not an IR shape we can steer from C.

## g4c addendum (17.10.2026)
`likely()/unlikely()` on the three `copy_{to,from}_user` failures and on the toggle test (4 variants, NOT tried before) only move `cameralog_ioctl` from 62 to 60 diff words in the ThinLTO emulation: not the fix, source unchanged.


## G55a (22.10.2026) - module-finish pass: block-level verdict for EVERY previously differing function (GLOBAL OS4.0.0.7 stock unless noted; no hardware, no git, no phone)
Method: real Kleaf build of the sources below (scratch <work-dir> bazel-out removed), compare_ko_v2 + preamble_cmp + tbl_audit3 (data tables by CONTENT), then for every differing function the raw `llvm-objdump -dr` listing of stock vs ours was read block by block (compact views + alpha/hunk diffs in work\a17\review\g55a_ev\ev\<module>.<function>.{stock.c,ours.c,diff,pdiff}; symcmp5b/s5ms used only as a corroborating filter). Evidence is disassembly of stock code: local review material, never export.
Build: sources unchanged; real Kleaf build md5 a16643104c3579dd5dd4a88b76e9f2d8; function set 11, kCFI 9/9, objects 18, imports 27 + CRC, exports 1, modinfo PASS; byte-identical 10 of 11 (unchanged).

| function | verdict | argument (raw-listing blocks) |
|---|---|---|
| cameralog_ioctl | EQUIVALENT-PROVEN | Only the toggle block (jump-table index 0 reads debugLevel and picks the OFF/ON store) is laid out differently: stock `ldr dbg; cmp #0; b.gt OFF; b ON` with OFF = `str wzr` and ON = `str #1`, ours `b.le ON` falling into OFF; both set x0 = 0 and join the epilogue. 7-entry byte jump table decoded: stock (base +0x6c) {0x00,0x4b,0x22,0x3f,0x3f,0x2c,0x30} -> {0x6c,0x198,0xf4,0x168,0x168,0x11c,0x12c}, ours {0x00,0x4b,0x2a,0x3f,0x3f,0x25,0x04} -> {0x6c,0x198,0x114,0x168,0x168,0x100,0x7c}: toggle / dump (memset 0x408 + 7 printk) / set (copy_from_user 4 -> debugLevel) / default -ENOTTY (x2) / ON / OFF in the same order; strnlen bounds 0x400/0x401, __check_object_size, copy_to_user, kfifo_out loop and every printk string identical (pdiff has no string hunk). |

Evidence files: `work\a17\review\g55a_ev\ev\cameralog.<function>.{stock.c,ours.c,diff,pdiff}`.
