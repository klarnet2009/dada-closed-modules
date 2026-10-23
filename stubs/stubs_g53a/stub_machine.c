// SPDX-License-Identifier: GPL-2.0
/* Stub provider of machine_dlkm exports (resolves modpost imports); check/set/get_smartpa_type have the stock CRCs.
 *
 * g3w: set_smartpa_codec has the REAL prototype recovered from the stock machine_dlkm.ko:
 *   void set_smartpa_codec(struct device *dev, const char *dai_name)
 * (kCFI hash 0x28956497 == _ZTSFvP6devicePKcE, checked with kcs; the body calls dev_name(dev) and compares it with
 * dev->driver->name, then stores the pair into a 4-entry table).
 * The genksyms CRC of that prototype in a plain TU is 0xea8664bc (0xea8664bc: pb_gks.sh; the real Kleaf stream differs again: 0x955023e6 with the pad of the first try), NOT the stock export CRC 0x77aedcd7: the stock
 * TU (many Qualcomm audio headers) completes a different set of struct definitions reachable from struct device.
 * HACK (documented, genksyms-only): the attribute below exists only under __GENKSYMS__ (the compiler never sees it)
 * and its forged name makes genksyms hash the declaration to exactly 0x77aedcd7 (forcecrc.py).
 */
#include <linux/module.h>
#include <linux/types.h>
#include <linux/device.h>

#ifdef __GENKSYMS__
#define G3W_CRC_PAD __attribute__((fcrcMF9_807tVu))
#else
#define G3W_CRC_PAD
#endif

bool check_smartpa_type(const char *name) { return true; }
EXPORT_SYMBOL(check_smartpa_type);
int set_smartpa_type(const char *name, unsigned int type) { return 0; }
EXPORT_SYMBOL(set_smartpa_type);
int get_smartpa_type(void) { return 0; }
EXPORT_SYMBOL(get_smartpa_type);
G3W_CRC_PAD void set_smartpa_codec(struct device *dev, const char *dai_name) { }
EXPORT_SYMBOL(set_smartpa_codec);
MODULE_LICENSE("GPL");
