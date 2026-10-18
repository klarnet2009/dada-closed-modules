// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 XiaoMi, Inc.
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include "tui_common.h"

static int (*tpd_tui_enter_func_request)(void);
static int (*tpd_tui_exit_func_request)(void);
static enum tpd_vendor_id_t (*tpd_tui_get_vendor_id_func_request)(void);

void register_tpd_tui_request(int (*enter_func)(void), int (*exit_func)(void), enum tpd_vendor_id_t (*get_vendor_id_func)(void))
{
	pr_info("[%s] request tui function\n", __func__);
	tpd_tui_enter_func_request = enter_func;
	tpd_tui_exit_func_request = exit_func;
	tpd_tui_get_vendor_id_func_request = get_vendor_id_func;
}
EXPORT_SYMBOL(register_tpd_tui_request);

int tpd_enter_tui(void)
{
	int ret = 0;

	pr_info("[%s] enter TUI+\n", __func__);
	if (tpd_tui_enter_func_request != NULL) {
		tpd_tui_enter_func_request();
		pr_info("[%s] enter func request success\n", __func__);
	}
	pr_info("[%s] enter TUI-\n", __func__);
	return ret;
}

int tpd_exit_tui(void)
{
	int ret = 0;

	pr_info("[%s] exit TUI+\n", __func__);
	if (tpd_tui_exit_func_request != NULL) {
		tpd_tui_exit_func_request();
		pr_info("[%s] exit func request success\n", __func__);
	}
	pr_info("[%s] exit TUI-\n", __func__);
	return ret;
}

enum tpd_vendor_id_t tpd_tui_get_vendor_id(void)
{
	enum tpd_vendor_id_t vendor_id = INVALID_VENDOR_ID;

	pr_info("[%s] get vendor id+\n", __func__);
	if (tpd_tui_get_vendor_id_func_request != NULL) {
		vendor_id = tpd_tui_get_vendor_id_func_request();
		pr_info("[%s] get vendor id: %d\n", __func__, vendor_id);
	}
	pr_info("[%s] get vendor id TUI-\n", __func__);
	return vendor_id;
}
