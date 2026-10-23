/* SPDX-License-Identifier: Dual BSD/GPL */
#ifndef MIBBR_INTERNAL_H
#define MIBBR_INTERNAL_H

#include <linux/types.h>

struct sock;
struct proc_dir_entry;

/* tcp_mibbr.c */
int xm_set_mibbr(struct sock *sk);
int is_trace_id(int uid);
int mibbr_register(void);
void mibbr_unregister(void);

/* mibbr_config.c: uid white list kept in /proc/mibbr/config */
extern int bbr_uid_num;
extern int bbr_uid_list[];
extern struct proc_dir_entry *bbr_dir __asm__("bbr_dir.llvm.16381526642031928295") __attribute__((visibility("hidden")));
bool is_mibbr_white_app(int uid);
int update_bbr_uid_list(void);
int bbr_config_init(void);
void mibbr_config_exit(void);

#endif
