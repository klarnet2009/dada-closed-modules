/* SPDX-License-Identifier: GPL-2.0 */
/* strategy_soc_limit.h - API of mca_soc_limit.ko (prototype checked against the OS4 stock export CRC) */
#ifndef __STRATEGY_SOC_LIMIT_H__
#define __STRATEGY_SOC_LIMIT_H__

#include <linux/types.h>

void soc_limit_process(bool enable, int soc_limit_thre);

#endif
