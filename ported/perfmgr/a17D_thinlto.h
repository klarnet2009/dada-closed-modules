/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ThinLTO promotes file-local objects (string literals included) that are used by code inlined into another
 * translation unit to GLOBAL HIDDEN `name.llvm.<hash>`; the hash is the bitcode hash of the TU and cannot be
 * reproduced.  Name them like the stock module.  Example: const char x[] A17D_SYM(".str.llvm.123") = "text";
 */
#ifndef A17D_THINLTO_H
#define A17D_THINLTO_H
#define A17D_SYM(s) __asm__(s) __attribute__((visibility("hidden")))
#endif
