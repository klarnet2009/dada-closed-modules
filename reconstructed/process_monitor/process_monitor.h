/* SPDX-License-Identifier: GPL-2.0 */
/* Reconstructed from the stock process_monitor.ko (Xiaomi 15 "dada") by reverse engineering; not original source. */
#ifndef PROCESS_MONITOR_H
#define PROCESS_MONITOR_H

#include <linux/types.h>

struct proc_dir_entry;

extern struct proc_dir_entry *g_entry_process_monitor;

/* d_task.c: detection of long running D-state tasks */
int init_D_task(struct proc_dir_entry *parent);
void exit_D_task(struct proc_dir_entry *parent);

/* sig_record.c: record of signals sent between processes */
int init_signal_record(struct proc_dir_entry *parent);
void exit_signal_record(struct proc_dir_entry *parent);

#endif
