/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mca_business_misc.h
 *
 * mca misc business driver
 *
 * Copyright (c) 2023-2023 Xiaomi Technologies Co., Ltd.
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
#ifndef __BUSINESS_MISC_H__
#define __BUSINESS_MISC_H__

#define PROBE_CNT_MAX	50

struct business_misc {
	struct device *dev;
};

int business_votable_init(struct device *dev);

#endif /*__BUSINESS_MISC_H__*/

