# cameralog - PARTIAL (10 of 11 functions byte-identical vs the OS4 stock, re-checked by g4 17.10.2026)

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
