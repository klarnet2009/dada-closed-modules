/* SPDX-License-Identifier: GPL-2.0 */
/*
 * charger_partition.h
 *
 * charger partition
 *
 * Copyright (c) 2024-2024 Xiaomi Technologies Co., Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */

#ifndef CHARGER_PARTITION_H
#define CHARGER_PARTITION_H

#define CHARGER_WORK_DELAY_MS			5000
#define CHARGER_PARTITION_RETRY_TIMES	20

#define PARTITION_NAME					"charger"
#define CHARGER_PARTITION_MAXSIZE		(0x10000)
#define CHARGER_PARTITION_RWSIZE		(0x1000)  /* read 1 block one time, a total of 256 blocks */
#define CHARGER_PARTITION_OFFSET		0

#define UFSHCD "ufshcd"

#define PART_SECTOR_SIZE	(0x200)
#define PART_BLOCK_SIZE		(0x1000)
#define CHARGER_PARTITION_MAX_BLOCK_TRANSFERS	(128)

enum charger_partition_attr_list {
	MCA_PROP_CHARGER_PARTITION_MISHOW,
	MCA_PROP_CHARGER_PARTITION_POWEROFFMODE,
	MCA_PROP_CHARGER_PARTITION_PROP_EU_MODE,
};

#endif