/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * xm_compat_6657.h - build-environment emulation (see STATUS.md, goal-G12c).
 *
 * The OS4 stock mitee_dlkm was compiled against the headers of kernel 6.6.57-android15-8, our tree is 6.6.144.
 * Differences that show up in the stock object code and can be reproduced WITHOUT changing semantics:
 *  - hweight32(): the stock expands to the inline popcount sequence (clang __builtin_popcount lowering, no import),
 *    the 6.6.144 headers call __sw_hweight32() (an extra import with a CRC the stock does not have).
 *
 * NOT reproduced on purpose (would change behaviour):
 *  - poll_wait(): 6.6.144 has an smp_mb() (upstream lost-wakeup fix) that 6.6.57 lacks: the extra `dmb ish`
 *    of tui_poll / mitee_memlog_poll stays.
 * NOT reproducible with a shim (struct layout of the stock kernel): task_struct.mm offset (+0x5a8 stock vs
 * +0x5a0 here) in optee_open_session; out-of-line _copy_from_user/_copy_to_user inlining decisions.
 */
#ifndef XM_COMPAT_6657_H
#define XM_COMPAT_6657_H

#include <linux/bitops.h>

#undef hweight32
#define hweight32(w) ((unsigned int)__builtin_popcount((unsigned int)(w)))

#endif /* XM_COMPAT_6657_H */
