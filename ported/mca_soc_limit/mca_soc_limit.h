/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mca_soc_limit.h
 *
 * soc limit driver
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

#ifndef __SOC_LIMIT_H__
#define __SOC_LIMIT_H__

struct soc_limit_info {
    struct device *dev;
    bool soc_limit_enable;
    int curr_soc;
};

#endif /* __SOC_LIMIT_H__ */