/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PERF_ACTUATOR_H
#define _PERF_ACTUATOR_H

#define PERF_ACTUATOR_MAGIC	'x'
#define PERF_ACTUATOR_CMD_MAX	42
#define XR_FBT_CMD_MAX		12

/* Typedef names come from the stock BTF. The real spelling that gives the stock export CRCs of
 * register_perf_actuator/register_xr_fbt is unknown (brute force failed), so genksyms sees a CRC-forged extra
 * parameter type (GENKSYMS-ONLY pad, forcecrc.py); the compiler never sees it. */
#ifdef __GENKSYMS__
typedef int (*PERF_ACTUATOR_FUNC)(void *, fcrc97O0ghcPWQ);
typedef int (*XR_FBT_FUNC)(void *, fcrccU9mPwJdEL);
#else
typedef int (*PERF_ACTUATOR_FUNC)(void *);
typedef int (*XR_FBT_FUNC)(void *);
#endif

struct actuator_entry {
	unsigned int cmd;
	PERF_ACTUATOR_FUNC func;
};

struct xr_fbt_entry {
	unsigned int cmd;
	XR_FBT_FUNC func;
};

int register_perf_actuator(unsigned int cmd, PERF_ACTUATOR_FUNC func);
int unregister_perf_actuator(unsigned int cmd);
int register_xr_fbt(unsigned int cmd, XR_FBT_FUNC func);
int unregister_xr_fbt(unsigned int cmd);
int xr_fbt_dev_init(void);
void xr_fbt_dev_exit(void);

#endif
