/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ThinLTO promotes file-local objects/functions that are used by cross-TU inlined code to GLOBAL HIDDEN `name.llvm.<hash>`;
 * the hash is the bitcode hash of the TU and cannot be reproduced. Name them like the stock module:
 *   struct x name PA_PROMOTED(name, 123) = ...;     /  PA_PROMOTED_FN(name, 123) rettype name(...)
 */
#ifndef PORTA_THINLTO_H
#define PORTA_THINLTO_H
#define PA_PROMOTED(name, hash) __asm__(#name ".llvm." #hash) __attribute__((visibility("hidden")))
/* explicit symbol name, hidden (for promoted string literals / __func__ arrays: const char x[] PA_SYM(".str.14.llvm.123") = ...) */
#define PA_SYM(s) __asm__(s) __attribute__((visibility("hidden")))
#endif
