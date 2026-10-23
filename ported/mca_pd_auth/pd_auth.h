/* SPDX-License-Identifier: GPL-2.0 */
/*
 * pd_auth.h
 *
 * pd private auth driver
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
#ifndef __PD_AUTH_H__
#define __PD_AUTH_H__

/* a17A: public headers the original gets transitively (kept here so mca_pd_auth.c keeps its line numbers) */
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/module.h>
#include <mca/protocol/protocol_class.h>

#define PD_ROLE_SINK_FOR_ADAPTER 0
#define PD_ROLE_SOURCE_FOR_ADAPTER 1
#define PD_AUTH_UVDM_AUTH_DATA_LEN 16
#define PD_AUTH_UVDM_AUTH_STR_LEN 128
#define PD_AUTH_VDM_CMD_HEX_DATA_LEN 40

struct pd_auth_strategy {
	struct device *dev;
	int pd_verified_type;
};

enum pd_auth_attr_list    {
	PD_AUTH_NAME = 0,
	PD_AUTH_REQUEST_VDM_CMD,
	PD_AUTH_CURRENT_STATE,
	PD_AUTH_ADAPTER_ID,
	PD_AUTH_ADAPTER_SVID,
	PD_AUTH_VERIFY_PROCESS,
	PD_AUTH_USBPD_VERIFIED,
	PD_AUTH_CURRENT_PR,
	PD_AUTH_IS_PD_ADAPTER,
	PD_AUTH_USBPD_DATA_ROLE,
};

#endif /* __PD_AUTH_H__ */

