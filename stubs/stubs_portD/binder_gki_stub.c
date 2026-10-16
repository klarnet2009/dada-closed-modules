// SPDX-License-Identifier: GPL-2.0
/*
 * Stub provider of the binder_gki.ko exports (stand-in only so modpost resolves the imports of millet_binder).
 * The two exports whose prototypes mention struct binder_proc / the oem hook table get the stock CRC through a
 * genksyms-only declaration (see work/scripts/perfC/crcforge.py); the real prototypes are
 *   void oem_register_binder_hook(struct oem_binder_hook *set);
 *   int reset_isfrozen(int uid, int pid, int found, struct binder_proc *proc);
 */
#ifdef __GENKSYMS__
void oem_register_binder_hook(char (*)[sizeof("Jxjtc*.0")]); /* CRC forged: see crcforge.py */
void reset_isfrozen(char (*)[sizeof("rtdpqvbQ")]); /* CRC forged: see crcforge.py */
#endif
#include <linux/module.h>
#include <linux/sched.h>
#include "uapi/millet.h"
#include "binder_oem.h"

#ifndef __GENKSYMS__
void oem_register_binder_hook(struct oem_binder_hook *set)
{
}
#endif
EXPORT_SYMBOL_GPL(oem_register_binder_hook);

void query_binder_app_stat(int uid)
{
}
EXPORT_SYMBOL_GPL(query_binder_app_stat);

#ifndef __GENKSYMS__
int reset_isfrozen(int uid, int pid, int found, struct binder_proc *proc)
{
	return 0;
}
#endif
EXPORT_SYMBOL_GPL(reset_isfrozen);

MODULE_LICENSE("GPL");
