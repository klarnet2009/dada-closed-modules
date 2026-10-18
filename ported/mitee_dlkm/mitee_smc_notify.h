/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2023 XiaoMi, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _MITEE_SMC_NOTIFY_H_
#define _MITEE_SMC_NOTIFY_H_

#include <linux/types.h>


#define MITEE_NOTIFICATION_TYPE_CONNECT     0x01
#define MITEE_NOTIFICATION_TYPE_SIGNAL      0x02
#define MITEE_NOTIFICATION_TYPE_DATAFRAG    0x03
#define MITEE_NOTIFICATION_TYPE_DATA        0x04

struct mitee_comm_ops;

void mitee_smc_notify_init(const struct mitee_comm_ops *ops);

extern unsigned int mitee_smc_notify_connect(uint8_t module_id,  unsigned int magic_num);
extern int mitee_smc_notify_signal(uint8_t module_id, unsigned int token, const uint8_t challenge);
extern int mitee_smc_notify_data(uint8_t module_id, unsigned int token, const void *data, uint16_t data_size);
#endif
