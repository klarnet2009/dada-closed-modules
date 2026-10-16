// SPDX-License-Identifier: GPL-2.0
/* Stub provider (not the real driver) of the metis symbols imported by miloadtrace. */
#ifdef __GENKSYMS__
void register_ui_task_detect(char (*)[sizeof("?cHJqQ]B")]); /* CRC forged: see crcforge.py */
void mi_set_fas_req(char (*)[sizeof("6CVD&.x?")]); /* CRC forged: see crcforge.py */
#endif
#include <linux/module.h>
#include <linux/kernel.h>
#include <mi_ext/mi_sched.h>
#include <mi_ext/walt_api.h>

unsigned int perf_min_freq[2];
EXPORT_SYMBOL(perf_min_freq);

#ifndef __GENKSYMS__
void register_ui_task_detect(int type, task_detect_f ui_detect, task_detect_f ui_pipeline_detect)
{
}
#endif
EXPORT_SYMBOL(register_ui_task_detect);

void unregister_ui_task_detect(int type)
{
}
EXPORT_SYMBOL(unregister_ui_task_detect);

#ifndef __GENKSYMS__
void mi_set_fas_req(struct mi_task_struct *mi_tsk)
{
}
#endif
EXPORT_SYMBOL(mi_set_fas_req);

MODULE_LICENSE("GPL");
