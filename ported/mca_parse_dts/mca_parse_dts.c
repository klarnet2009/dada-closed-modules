// SPDX-License-Identifier: GPL-2.0
/*
 * mca_parse_dts.c
 *
 * dts parse interface for power module
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
#include <linux/module.h>
//#include <linux/kstrtox.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_parse_dts.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_dts"
#endif

int mca_parse_dts_u8(const struct device_node *np,
	const char *prop, u8 *data, u8 default_value)
{
	if (!np || !prop || !data) {
		mca_log_err("np or prop or data is null\n");
		return -EINVAL;
	}

	if (of_property_read_u8(np, prop, data)) {
		*data = default_value;
		mca_log_err("prop %s read fail, set default %u\n", prop, *data);
		return -EINVAL;
	}

	mca_log_debug("prop %s=%u\n", prop, *data);
	return 0;
}
EXPORT_SYMBOL(mca_parse_dts_u8);

int mca_parse_dts_u32(const struct device_node *np,
	const char *prop, u32 *data, u32 default_value)
{
	if (!np || !prop || !data) {
		mca_log_err("np or prop or data is null\n");
		return -EINVAL;
	}

	if (of_property_read_u32(np, prop, data)) {
		*data = default_value;
		mca_log_err("prop %s read fail, set default %u\n", prop, *data);
		return -EINVAL;
	}

	mca_log_debug("prop %s=%u\n", prop, *data);
	return 0;
}
EXPORT_SYMBOL(mca_parse_dts_u32);

int mca_parse_dts_u8_array(const struct device_node *np,
	const char *prop, u8 *data, u16 len)
{
	if (!np || !prop || !data) {
		mca_log_err("np or prop or data is null\n");
		return -EINVAL;
	}

	if (of_property_read_u8_array(np, prop, data, len)) {
		mca_log_err("prop %s read fail, array len %d\n", prop, len);
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(mca_parse_dts_u8_array);

int mca_parse_dts_u32_array(const struct device_node *np,
	const char *prop, u32 *data, u32 len)
{
	if (!np || !prop || !data) {
		mca_log_err("np or prop or data is null\n");
		return -EINVAL;
	}

	if (of_property_read_u32_array(np, prop, data, len)) {
		mca_log_err("prop %s read fail, array len %d\n", prop, len);
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(mca_parse_dts_u32_array);

int mca_parse_dts_u8_count(const struct device_node *np,
	const char *prop, u32 row, u32 col)
{
	int len;

	if (!np || !prop) {
		mca_log_err("np or prop is null\n");
		return -EINVAL;
	}

	len = of_property_count_u8_elems(np, prop);
	if ((len <= 0) || ((unsigned int)len % col != 0) || ((unsigned int)len > row * col)) {
		mca_log_err("prop %s length read fail\n", prop);
		return -EINVAL;
	}

	mca_log_debug("prop %s length=%d\n", prop, len);
	return len;
}
EXPORT_SYMBOL(mca_parse_dts_u8_count);

int mca_parse_dts_u32_count(const struct device_node *np,
	const char *prop, u32 row, u32 col)
{
	int len;

	if (!np || !prop) {
		mca_log_err("np or prop is null\n");
		return -EINVAL;
	}

	len = of_property_count_u32_elems(np, prop);
	if ((len <= 0) || ((unsigned int)len % col != 0) || ((unsigned int)len > row * col)) {
		mca_log_err("prop %s length read fail\n", prop);
		return -EINVAL;
	}

	mca_log_debug("prop %s length=%d\n", prop, len);
	return len;
}
EXPORT_SYMBOL(mca_parse_dts_u32_count);

int mca_parse_dts_u32_index(const struct device_node *np,
	const char *prop, int index, u32 *data)
{
	if (!np || !prop || !data) {
		mca_log_err("np or prop or data is null\n");
		return -EINVAL;
	}

	if (of_property_read_u32_index(np, prop, index, data)) {
		mca_log_err("prop %s[%d] read fail\n", prop, index);
		return -EINVAL;
	}

	mca_log_debug("prop %s[%d]=%u\n", prop, index, *data);
	return 0;
}
EXPORT_SYMBOL(mca_parse_dts_u32_index);

int mca_parse_dts_string(const struct device_node *np,
	const char *prop, const char **out)
{
	if (!np || !prop || !out) {
		mca_log_err(" np or prop or out is null\n");
		return -EINVAL;
	}

	if (of_property_read_string(np, prop, out)) {
		mca_log_err("prop %s read fail\n", prop);
		return -EINVAL;
	}

	mca_log_debug("prop %s=%s\n", prop, *out);
	return 0;
}
EXPORT_SYMBOL(mca_parse_dts_string);

int mca_parse_dts_string_index(const struct device_node *np,
	const char *prop, int index, const char **out)
{
	if (!np || !prop || !out) {
		mca_log_err("np or prop or out is null\n");
		return -EINVAL;
	}

	if (of_property_read_string_index(np, prop, index, out)) {
		mca_log_err("prop %s[%d] read fail\n", prop, index);
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(mca_parse_dts_string_index);

int mca_parse_dts_count_strings(const struct device_node *np,
	const char *prop, u32 row, u32 col)
{
	int len;

	if (!np || !prop) {
		mca_log_err("np or prop is null\n");
		return -EINVAL;
	}

	len = of_property_count_strings(np, prop);
	if ((len <= 0) || ((unsigned int)len % col != 0) || ((unsigned int)len > row * col)) {
		mca_log_err("prop %s length read fail\n", prop);
		return -EINVAL;
	}

	mca_log_debug("prop %s length=%d\n", prop, len);
	return len;
}
EXPORT_SYMBOL(mca_parse_dts_count_strings);

int mca_parse_dts_string_array(const struct device_node *np,
	const char *prop, int *data, u32 row, u32 col)
{
	int i, len;
	const char *tmp_string = NULL;

	len = mca_parse_dts_count_strings(np, prop, row, col);
	if (len < 0)
		return -EINVAL;

	for (i = 0; i < len; i++) {
		if (mca_parse_dts_string_index(np, prop, i, &tmp_string))
			return -EINVAL;

		if (kstrtoint(tmp_string, 0, &data[i]))
			return -EINVAL;
	}

	return len;
}
EXPORT_SYMBOL(mca_parse_dts_string_array);

MODULE_DESCRIPTION("mca parse dts");
MODULE_AUTHOR("liyuze1@xiaomi.com");
MODULE_LICENSE("GPL v2");

